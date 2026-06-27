# Character Device Driver

## What it demonstrates
- Character device lifecycle: open → read → write → close
- file_operations structure — the core of any char driver
- copy_to_user / copy_from_user — safe kernel<->user data transfer
- Dynamic major number allocation
- Automatic /dev/ node creation via class/device API
- Mutex for exclusive device access

## Relevance to ONU firmware (EN7528HU TCLinux)
This is exactly how /dev/omci works:
  - pon_mac.ko creates the /dev/omci character device
  - omci_app opens /dev/omci and reads OMCI frames from it
  - OLT -> fiber -> pon_mac.ko -> /dev/omci -> omci_app (userspace)

## Build
```bash
make
```

## Test
```bash
sudo insmod chardev.ko
dmesg | tail -5

# Write data to the device
echo "hello kernel" | sudo tee /dev/mydev

# Read it back
cat /dev/mydev

# Check kernel logs
dmesg | tail -10

# Unload the driver
sudo rmmod chardev
dmesg | tail -5
```

## Imp points
1. file_operations maps system calls (open/read/write) to driver functions
2. copy_to_user/copy_from_user needed because kernel and user have
   separate virtual address spaces — cannot dereference user pointers directly
3. Major number identifies the driver, minor number identifies the device instance
4. Mutex prevents two processes opening the device simultaneously
5. This pattern is identical to /dev/omci in ONU firmware —
   pon_mac.ko creates it, omci_app reads OMCI frames through it
