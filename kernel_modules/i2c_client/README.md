# I2C Client HAL Driver

## What it demonstrates
- I2C driver probe/remove lifecycle
- Device Tree matching via of_match_table
- i2c_smbus_read/write_byte_data register access
- devm_ managed memory (auto-freed on remove)
- Per-device private data via i2c_set/get_clientdata
- i2c_check_functionality for capability verification

## Relevance to ONU firmware (EN7528HU TCLinux)
On EN7528HU, I2C is used to read the SFP optical transceiver
via DDMI (Digital Diagnostic Monitoring Interface):
  - I2C address 0x50 -> SFP EEPROM (vendor, model, wavelength)
  - I2C address 0x51 -> DDMI real-time diagnostics:
      TX power (dBm)
      RX power (dBm)
      Temperature (C)
      Supply voltage (V)
      Laser bias current (mA)

ddmi_drv.ko on EN7528HU follows this exact same probe pattern.
The I2C HAL (i2c-econet.ko) provides the bus, ddmi_drv sits on top.

## Device Tree entry (add to your board .dts)
```dts
&i2c0 {
    status = "okay";
    my_sensor: my-device@48 {
        compatible = "myvendor,my-i2c-device";
        reg = <0x48>;
    };
};
```

## Build
```bash
make
```

## Test without real hardware (using i2c-stub)
```bash
# Load stub I2C adapter simulating device at 0x48
sudo modprobe i2c-stub chip_addr=0x48

# Verify stub loaded
ls /dev/i2c-*
i2cdetect -y 1   # should show 0x48

# Load our driver
sudo insmod i2c_client.ko
dmesg | tail -10

# Write/read registers manually
i2cset -y 1 0x48 0x02 0x01    # write control register
i2cget -y 1 0x48 0x00         # read chip ID

# Unload
sudo rmmod i2c_client
sudo rmmod i2c-stub
```

## imp points
1. probe() is called by kernel when DTB compatible string matches driver
2. devm_kzalloc auto-frees memory when driver is removed — prevents leaks
3. i2c_set_clientdata stores per-device state — supports multiple device instances
4. i2c_check_functionality verifies the I2C adapter supports needed operations
5. This is the exact pattern used by ddmi_drv on EN7528HU to read
   SFP optical power levels over I2C from the transceiver
