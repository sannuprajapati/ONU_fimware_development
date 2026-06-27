# Linux Kernel Driver Modules — Interview Practice Kit

## Overview
5 practical kernel modules demonstrating real embedded Linux
driver patterns used in ONU firmware (EN7528HU TCLinux).

## Modules

| Module | What it shows | ONU Relevance |
|--------|--------------|---------------|
| chardev | char device, file_ops, copy_to/from_user | /dev/omci pattern |
| gpio_led | GPIO HAL, kernel timer, blink | LED control in gpio-econet.ko |
| i2c_client | I2C probe, DTB match, smbus read/write | ddmi_drv SFP diagnostics |
| platform_drv | ioremap, readl/writel, IRQ, devm_ | ponhal.ko register access |
| proc_module | procfs, seq_file, state machine | /proc/tc3162/pon_status |

## Quick start
```bash
# Build all modules
make all

# Load a specific module
cd chardev && make && sudo insmod chardev.ko

# Run demo
make demo
```

## Software Stack Context (EN7528HU TCLinux)
```
L7  tr069d  httpd  dropbear  dnsmasq     <- Application
L6  libcmm.so  libdb.so  libssl.so       <- Middleware
L5  pppd  voipd  udhcpc                  <- Session
L4  iptables  tc  nf_conntrack           <- Transport
L3  hwnat.ko  ip routing  net/ipv4/      <- Network
L2  pon_mac.ko  omci_app  /dev/omci      <- Data Link  <-- chardev models this
L1  GPON protocol (GEM/GTC)              <- Physical
──────────────────────────── OSI boundary
HAL ponhal.ko  econet_eth.ko             <- HAL        <-- platform_drv models this
HAL i2c-econet.ko  spi-econet.ko        <- Bus HAL    <-- i2c_client models this
HAL gpio-econet.ko                       <- GPIO HAL   <-- gpio_led models this
BSP arch/mips/econet/                    <- BSP
BLD tcboot.bin (mtd0)                    <- Bootloader
SoC EN7528HU silicon                     <- Hardware
```

## Prerequisites
```bash
# Ubuntu/Debian
sudo apt install linux-headers-$(uname -r) build-essential

# Raspberry Pi (Raspbian)
sudo apt install raspberrypi-kernel-headers build-essential
```

## Interview script
When asked about kernel/driver experience:

"I worked on EN7528HU ONU firmware running TCLinux 3.18.21.
The firmware stack runs from tcboot bootloader through BSP in
arch/mips/econet/, HAL drivers like ponhal.ko and pon_mac.ko
in modules/private/ko/, up through OSI layers to omci_app
and tr069d daemons.

I understand how Device Tree in the BSP triggers HAL driver probe,
how /dev/omci bridges pon_mac.ko to omci_app userspace,
and how the dual-bank OTA upgrade works via the reservearea
boot flag in SPI-NAND flash.

These modules demonstrate the core driver patterns I work with."
