/*
 * platform_drv.c - Platform Device HAL Driver
 *
 * Demonstrates:
 *   - Platform driver probe/remove lifecycle
 *   - Device Tree resource extraction (ioremap)
 *   - Memory-mapped register read/write (readl/writel)
 *   - Interrupt request (request_irq)
 *   - devm_ managed resources
 *   - BSP -> HAL driver link via Device Tree
 *
 * Relevance to ONU firmware (EN7528HU TCLinux):
 *   This is the exact pattern of ponhal.ko and econet_eth.ko.
 *   BSP (arch/mips/econet/en7528.dtsi) defines the hardware
 *   address and IRQ. Platform driver probes against DTB,
 *   calls ioremap to map registers, then uses readl/writel
 *   to access EN7528 silicon directly.
 *
 *   en7528.dtsi example:
 *     pon_mac: pon@1fbe0000 {
 *         compatible = "econet,en7528-pon";
 *         reg = <0x1fbe0000 0x1000>;
 *         interrupts = <GIC_SPI 23 IRQ_TYPE_LEVEL_HIGH>;
 *     };
 *
 * Test:
 *   sudo insmod platform_drv.ko
 *   cat /proc/myplatform_status
 *   dmesg | tail -15
 *   sudo rmmod platform_drv
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/* Imaginary hardware register offsets */
#define REG_CTRL        0x00   /* Control register         */
#define REG_STATUS      0x04   /* Status register          */
#define REG_IRQ_STATUS  0x08   /* Interrupt status         */
#define REG_IRQ_MASK    0x0C   /* Interrupt mask           */
#define REG_DATA        0x10   /* Data register            */
#define REG_VERSION     0x14   /* HW version register      */

/* Control register bits */
#define CTRL_ENABLE     BIT(0)
#define CTRL_RESET      BIT(1)
#define CTRL_IRQ_EN     BIT(2)

/* Status register bits */
#define STATUS_READY    BIT(0)
#define STATUS_BUSY     BIT(1)
#define STATUS_ERROR    BIT(2)

/*
 * Per-device private data
 * One instance per platform device
 */
struct my_platform_data {
    void __iomem         *base;       /* mapped register base  */
    int                   irq;        /* interrupt number      */
    u32                   hw_version; /* read from HW register */
    u32                   irq_count;  /* interrupt counter     */
    struct proc_dir_entry *proc_entry;
    struct platform_device *pdev;
};

/*
 * my_irq_handler - interrupt service routine
 *
 * Called when hardware fires an interrupt.
 * On EN7528HU, ponhal.ko ISR handles TX burst completion,
 * RX data ready, and LOS (Loss of Signal) events.
 *
 * Rules in IRQ context:
 *   - Cannot sleep
 *   - Cannot use mutex
 *   - Must be fast
 */
static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
    struct my_platform_data *hw = dev_id;
    u32 irq_status;

    /* read and clear interrupt status */
    irq_status = readl(hw->base + REG_IRQ_STATUS);
    writel(irq_status, hw->base + REG_IRQ_STATUS);

    if (!irq_status)
        return IRQ_NONE;  /* not our interrupt */

    hw->irq_count++;
    pr_debug("platform_drv: IRQ %d fired — status=0x%08x count=%u\n",
             irq, irq_status, hw->irq_count);

    return IRQ_HANDLED;
}

/* procfs show function — cat /proc/myplatform_status */
static int myplatform_proc_show(struct seq_file *m, void *v)
{
    struct my_platform_data *hw = m->private;
    u32 status, data;

    status = readl(hw->base + REG_STATUS);
    data   = readl(hw->base + REG_DATA);

    seq_printf(m, "HW Version : 0x%08x\n", hw->hw_version);
    seq_printf(m, "Status Reg : 0x%08x\n", status);
    seq_printf(m, "Data Reg   : 0x%08x\n", data);
    seq_printf(m, "IRQ Count  : %u\n",     hw->irq_count);
    seq_printf(m, "Ready      : %s\n",
               (status & STATUS_READY) ? "yes" : "no");
    seq_printf(m, "Error      : %s\n",
               (status & STATUS_ERROR) ? "YES" : "no");
    return 0;
}

static int myplatform_proc_open(struct inode *inode,
                                 struct file *file)
{
    return single_open(file, myplatform_proc_show,
                       PDE_DATA(inode));
}

static const struct file_operations proc_fops = {
    .owner   = THIS_MODULE,
    .open    = myplatform_proc_open,
    .read    = seq_read,
    .release = single_release,
};

/*
 * my_platform_probe - called when DTB compatible string matches
 *
 * This is triggered by BSP (arch/mips/econet/en7528.dtsi)
 * registering the platform device, which then matches
 * our my_of_match table entry.
 *
 * Sequence in EN7528HU:
 *   1. BSP parses DTB in setup_arch()
 *   2. Creates platform_device for "econet,en7528-pon"
 *   3. Kernel calls ponhal.ko probe()
 *   4. ponhal.ko calls ioremap to map 0x1fbe0000
 *   5. ponhal.ko requests IRQ
 */
static int my_platform_probe(struct platform_device *pdev)
{
    struct my_platform_data *hw;
    struct resource         *res;
    int ret;

    dev_info(&pdev->dev, "probing platform device\n");

    /*
     * devm_kzalloc: allocate private data.
     * devm_ prefix = device-managed, auto-freed on remove.
     */
    hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
    if (!hw)
        return -ENOMEM;

    hw->pdev = pdev;

    /*
     * Step 1: get register base address from DTB
     * platform_get_resource reads the "reg" property:
     *   reg = <0x1fbe0000 0x1000>;
     */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(&pdev->dev, "no memory resource\n");
        return -ENODEV;
    }

    /*
     * Step 2: ioremap — maps physical hardware register
     * address into kernel virtual address space.
     * After this, readl/writel access the real silicon.
     *
     * devm_ioremap_resource auto-unmaps on remove.
     */
    hw->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(hw->base)) {
        dev_err(&pdev->dev, "ioremap failed\n");
        return PTR_ERR(hw->base);
    }

    dev_info(&pdev->dev,
             "registers mapped: phys=0x%08x size=0x%x\n",
             (u32)res->start,
             (u32)resource_size(res));

    /*
     * Step 3: read hardware version register
     * readl = 32-bit memory-mapped register read
     * This directly reads from silicon at the mapped address
     */
    hw->hw_version = readl(hw->base + REG_VERSION);
    dev_info(&pdev->dev, "HW version = 0x%08x\n",
             hw->hw_version);

    /*
     * Step 4: get IRQ number from DTB
     * Reads "interrupts" property from device tree
     */
    hw->irq = platform_get_irq(pdev, 0);
    if (hw->irq < 0) {
        dev_warn(&pdev->dev,
                 "no IRQ defined — polling mode\n");
        hw->irq = -1;
    } else {
        /*
         * Step 5: request IRQ — register ISR
         * IRQF_SHARED allows sharing with other devices
         */
        ret = devm_request_irq(&pdev->dev,
                                hw->irq,
                                my_irq_handler,
                                IRQF_SHARED,
                                "my_platform",
                                hw);
        if (ret) {
            dev_err(&pdev->dev,
                    "request_irq %d failed: %d\n",
                    hw->irq, ret);
            return ret;
        }
        dev_info(&pdev->dev, "IRQ %d registered\n",
                 hw->irq);
    }

    /*
     * Step 6: initialize hardware
     * writel = 32-bit memory-mapped register write
     * Enable device + interrupt via control register
     */
    writel(CTRL_RESET, hw->base + REG_CTRL);   /* reset */
    writel(0, hw->base + REG_CTRL);             /* clear */
    writel(CTRL_ENABLE | CTRL_IRQ_EN,
           hw->base + REG_CTRL);                /* enable */

    /*
     * Step 7: store private data for later retrieval
     */
    platform_set_drvdata(pdev, hw);

    /*
     * Step 8: create /proc/myplatform_status
     */
    hw->proc_entry = proc_create_data("myplatform_status",
                                       0444, NULL,
                                       &proc_fops, hw);

    dev_info(&pdev->dev,
             "probe complete — /proc/myplatform_status created\n");
    return 0;
}

/*
 * my_platform_remove - called on rmmod or device unplug
 */
static int my_platform_remove(struct platform_device *pdev)
{
    struct my_platform_data *hw = platform_get_drvdata(pdev);

    /* disable hardware */
    writel(0, hw->base + REG_CTRL);

    /* remove proc entry */
    if (hw->proc_entry)
        proc_remove(hw->proc_entry);

    dev_info(&pdev->dev,
             "removed — total IRQs handled: %u\n",
             hw->irq_count);

    /* devm_ resources (ioremap, irq, memory)
       are automatically released here */
    return 0;
}

/*
 * Device Tree match table
 * Kernel matches "compatible" property in DTB against this table.
 * This is the BSP -> HAL driver link.
 */
static const struct of_device_id my_of_match[] = {
    { .compatible = "myvendor,my-platform" },
    { }  /* sentinel */
};
MODULE_DEVICE_TABLE(of, my_of_match);

/*
 * platform_driver structure
 */
static struct platform_driver my_platform_driver = {
    .probe  = my_platform_probe,
    .remove = my_platform_remove,
    .driver = {
        .name           = "my-platform",
        .of_match_table = my_of_match,
        .owner          = THIS_MODULE,
    },
};

/*
 * module_platform_driver macro expands to:
 *   module_init -> platform_driver_register()
 *   module_exit -> platform_driver_unregister()
 */
module_platform_driver(my_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ONU Firmware Engineer");
MODULE_DESCRIPTION("Platform HAL Driver — models ponhal.ko/econet_eth.ko pattern");
MODULE_VERSION("1.0");
