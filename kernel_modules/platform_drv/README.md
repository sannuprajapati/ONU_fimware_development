# Platform HAL Driver

## What it demonstrates
- Platform driver probe/remove lifecycle
- Device Tree resource extraction (platform_get_resource)
- ioremap: maps physical hardware registers into kernel virtual space
- readl/writel: direct memory-mapped register access (real silicon)
- request_irq: interrupt handler registration
- devm_ managed resources (auto-released on remove)
- BSP -> HAL driver link via Device Tree compatible string

## Relevance to ONU firmware (EN7528HU TCLinux)
This is the exact pattern used by ponhal.ko and econet_eth.ko:

BSP (arch/mips/econet/en7528.dtsi) defines:
  pon_mac: pon@1fbe0000 {
      compatible = "econet,en7528-pon";
      reg = <0x1fbe0000 0x1000>;
      interrupts = <GIC_SPI 23 IRQ_TYPE_LEVEL_HIGH>;
  };

ponhal.ko probe():
  1. platform_get_resource() reads reg = <0x1fbe0000 0x1000>
  2. devm_ioremap_resource() maps 0x1fbe0000 into kernel VA space
  3. readl/writel access EN7528 PON MAC registers directly
  4. devm_request_irq() registers ISR for PON burst events

This driver follows identical structure — just with imaginary addresses.

## Build
```bash
make
```

## Test
```bash
sudo insmod platform_drv.ko
dmesg | tail -15

# View hardware status via procfs
cat /proc/myplatform_status

# Unload
sudo rmmod platform_drv
dmesg | tail -5
```

Note: probe() will only be called if a matching DTB entry exists.
Without hardware, the driver loads but probe() is not triggered.
Use dmesg to verify module load. For full testing, add DTB entry
or use platform_device_register() in a test module.

## Imp points
1. ioremap maps physical silicon register address to kernel VA —
   after this, readl(base + 0x10) directly reads hardware register
2. devm_ioremap_resource auto-unmaps on driver remove — no memory leak
3. IRQ handler cannot sleep — must be fast, use tasklet/workqueue for heavy work
4. platform_get_resource extracts "reg" property from DTB — this is how
   BSP communicates hardware addresses to HAL drivers
5. This is exactly how ponhal.ko accesses EN7528 optical transceiver
   registers on the EN7528HU ONU board
