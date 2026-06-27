# Procfs Driver Module

## What it demonstrates
- Creating /proc/ directory and multiple entries
- seq_file API for clean multi-line kernel output
- Writable proc entry (control interface)
- Atomic operations for thread-safe statistics
- State machine modeling (PLOAM O1-O5 states)
- Module parameters (device_name)

## Relevance to ONU firmware (EN7528HU TCLinux)
/proc/tc3162/ in TCLinux exposes vendor driver state:
  /proc/tc3162/pon_status    <- PON registration state (O1-O5)
  /proc/tc3162/hwnat         <- Hardware NAT statistics
  /proc/tc3162/gmac          <- Ethernet MAC state
  /proc/tc3162/omci          <- OMCI connection status

ponmgr reads /proc/tc3162/pon_status to track fiber registration.
This module creates the same pattern with /proc/mydriver/.

The state machine in this module directly models GPON PLOAM states:
  STATE_IDLE        = PLOAM O1 (powered on)
  STATE_INIT        = PLOAM O2 (standby)
  STATE_DETECTING   = PLOAM O3 (serial number detection)
  STATE_RANGING     = PLOAM O4 (ranging)
  STATE_OPERATIONAL = PLOAM O5 (registered, operational)

## Build
```bash
make
```

## Test (best for live interview demo)
```bash
# Load
sudo insmod proc_module.ko
dmesg | tail -5

# Read status
cat /proc/mydriver/status

# Read stats
cat /proc/mydriver/stats

# Control state machine (like PLOAM transitions)
echo "init"  | sudo tee /proc/mydriver/control
cat /proc/mydriver/status   # shows INIT state

echo "start" | sudo tee /proc/mydriver/control
echo "next"  | sudo tee /proc/mydriver/control
echo "next"  | sudo tee /proc/mydriver/control
cat /proc/mydriver/status   # shows OPERATIONAL state

# Check write stats updated
cat /proc/mydriver/stats

# Reset everything
echo "reset" | sudo tee /proc/mydriver/control

# Run full test
make test

# Unload
sudo rmmod proc_module
```

##  points
1. seq_file handles large output correctly — splits across multiple read() calls
   unlike sprintf to a static buffer which can overflow
2. atomic_t for counters — safe from multiple CPUs without mutex
3. Proc write interface models how ponmgr triggers state transitions in ONU firmware
4. /proc is a virtual filesystem — no disk I/O, reads call your show() function directly
5. This is exactly how /proc/tc3162/pon_status works on EN7528HU —
   ponhal.ko creates the entry, ponmgr reads it to check registration state
