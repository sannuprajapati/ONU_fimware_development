/*
 * i2c_client.c - I2C Client HAL Driver
 *
 * Demonstrates:
 *   - I2C driver probe/remove lifecycle
 *   - Device Tree matching via of_match_table
 *   - i2c_smbus read/write register access
 *   - devm_ managed memory allocation
 *   - Per-device private data via i2c_set_clientdata
 *
 * Relevance to ONU firmware (EN7528HU TCLinux):
 *   On EN7528HU, I2C is used to read SFP/optical
 *   transceiver EEPROM (DDMI — Digital Diagnostic
 *   Monitoring Interface). The ddmi_drv reads optical
 *   TX power, RX power, temperature, voltage via I2C
 *   at address 0x50/0x51 on the SFP module.
 *   This driver follows the exact same pattern.
 *
 * Device Tree entry needed (add to your .dts):
 *   &i2c0 {
 *       my_sensor: my-device@48 {
 *           compatible = "myvendor,my-i2c-device";
 *           reg = <0x48>;
 *       };
 *   };
 *
 * Test without real hardware:
 *   Use i2c-stub kernel module to simulate I2C device.
 *   sudo modprobe i2c-stub chip_addr=0x48
 *   sudo insmod i2c_client.ko
 *   dmesg | tail -10
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/err.h>

/* Register map of the imaginary I2C device */
#define REG_CHIP_ID      0x00   /* Chip ID register */
#define REG_STATUS       0x01   /* Status register  */
#define REG_CTRL         0x02   /* Control register */
#define REG_DATA_MSB     0x03   /* Data MSB         */
#define REG_DATA_LSB     0x04   /* Data LSB         */

#define EXPECTED_CHIP_ID 0xAB   /* Expected chip ID */

/*
 * Per-device private data structure.
 * Allocated once per probe, freed automatically
 * by devm on remove.
 */
struct my_i2c_data {
    struct i2c_client *client;
    u8                 chip_id;
    u16                last_reading;
};

/*
 * my_read_reg - read a single 8-bit register
 *
 * i2c_smbus_read_byte_data performs:
 *   START -> addr+W -> reg -> RESTART -> addr+R -> data -> STOP
 */
static int my_read_reg(struct i2c_client *client,
                        u8 reg, u8 *val)
{
    int ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev,
                "read reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    *val = (u8)ret;
    return 0;
}

/*
 * my_write_reg - write a single 8-bit register
 *
 * i2c_smbus_write_byte_data performs:
 *   START -> addr+W -> reg -> data -> STOP
 */
static int my_write_reg(struct i2c_client *client,
                         u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) {
        dev_err(&client->dev,
                "write reg 0x%02x = 0x%02x failed: %d\n",
                reg, val, ret);
    }
    return ret;
}

/*
 * my_read_data - read 16-bit value (MSB + LSB registers)
 */
static int my_read_data(struct i2c_client *client, u16 *val)
{
    u8 msb, lsb;
    int ret;

    ret = my_read_reg(client, REG_DATA_MSB, &msb);
    if (ret) return ret;

    ret = my_read_reg(client, REG_DATA_LSB, &lsb);
    if (ret) return ret;

    *val = ((u16)msb << 8) | lsb;
    return 0;
}

/*
 * my_i2c_probe - called when kernel matches device to driver
 *
 * This is triggered by the Device Tree match:
 *   compatible = "myvendor,my-i2c-device"
 *
 * Same mechanism as how i2c-econet.ko is probed on EN7528HU
 * when DTB entry for I2C controller is matched.
 */
static int my_i2c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct my_i2c_data *data;
    u8  chip_id;
    u16 reading;
    int ret;

    dev_info(&client->dev,
             "probing I2C device at addr 0x%02x\n",
             client->addr);

    /* Verify I2C functionality */
    if (!i2c_check_functionality(client->adapter,
                                  I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev,
                "I2C adapter does not support SMBUS byte\n");
        return -EOPNOTSUPP;
    }

    /* Read chip ID to verify correct device */
    ret = my_read_reg(client, REG_CHIP_ID, &chip_id);
    if (ret)
        return ret;

    dev_info(&client->dev, "chip ID = 0x%02x\n", chip_id);

    /*
     * devm_kzalloc: managed allocation.
     * Automatically freed when driver is removed.
     * No need to manually kfree in remove().
     */
    data = devm_kzalloc(&client->dev,
                         sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client  = client;
    data->chip_id = chip_id;

    /* Store private data — retrieved in other functions */
    i2c_set_clientdata(client, data);

    /* Enable the device via control register */
    ret = my_write_reg(client, REG_CTRL, 0x01);
    if (ret)
        return ret;

    /* Do initial reading */
    ret = my_read_data(client, &reading);
    if (ret == 0) {
        data->last_reading = reading;
        dev_info(&client->dev,
                 "initial reading = %u\n", reading);
    }

    dev_info(&client->dev,
             "I2C device probed successfully\n");
    return 0;
}

/*
 * my_i2c_remove - called when device is removed or driver unloaded
 */
static int my_i2c_remove(struct i2c_client *client)
{
    struct my_i2c_data *data = i2c_get_clientdata(client);

    /* disable device */
    my_write_reg(client, REG_CTRL, 0x00);

    dev_info(&client->dev,
             "removed — last reading was %u\n",
             data->last_reading);
    return 0;
}

/*
 * Device Tree match table.
 * Kernel matches this against DTB "compatible" property.
 * This is exactly how BSP triggers HAL driver probe.
 */
static const struct of_device_id my_i2c_of_match[] = {
    { .compatible = "myvendor,my-i2c-device" },
    { }  /* sentinel */
};
MODULE_DEVICE_TABLE(of, my_i2c_of_match);

/*
 * I2C device ID table (non-DT systems)
 */
static const struct i2c_device_id my_i2c_id[] = {
    { "my-i2c-device", 0 },
    { }  /* sentinel */
};
MODULE_DEVICE_TABLE(i2c, my_i2c_id);

/*
 * i2c_driver structure — registers driver with I2C subsystem
 */
static struct i2c_driver my_i2c_driver = {
    .driver = {
        .name           = "my-i2c-device",
        .of_match_table = my_i2c_of_match,
    },
    .probe    = my_i2c_probe,
    .remove   = my_i2c_remove,
    .id_table = my_i2c_id,
};

/*
 * module_i2c_driver macro expands to:
 *   module_init that calls i2c_add_driver()
 *   module_exit that calls i2c_del_driver()
 */
module_i2c_driver(my_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ONU Firmware Engineer");
MODULE_DESCRIPTION("I2C HAL Driver — models ddmi_drv/SFP EEPROM pattern");
MODULE_VERSION("1.0");
