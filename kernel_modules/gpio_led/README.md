# GPIO LED Blink Driver

## What it demonstrates
- GPIO HAL API: gpio_request, gpio_direction_output,
  gpio_set_value, gpio_free
- Kernel timer: timer_list, mod_timer, del_timer_sync
- Module parameters via module_param()
- softirq context constraints (no sleeping in timer callback)
- Proper cleanup in __exit

## Relevance to ONU firmware (EN7528HU TCLinux)
gpio-econet.ko uses the identical GPIO HAL API to control:
  - PON registered LED (green)
  - LOS (Loss of Signal) alarm LED (red)
  - Power LED
  - WPS button GPIO input
  - Factory reset button GPIO input

The GPIO HAL (gpio-econet.ko) hides the EN7528 GPIO register
addresses from this code — same abstraction principle.

## Hardware setup
- Connect LED + 330 ohm resistor between GPIO_PIN and GND
- Raspberry Pi: GPIO 18 = physical pin 12 (BCM numbering)
- Change gpio_pin parameter for other boards

## Build
```bash
make
```

## Test
```bash
# Load with defaults (GPIO 18, 500ms blink)
sudo insmod gpio_led.ko

# Load with custom parameters
sudo insmod gpio_led.ko gpio_pin=18 blink_rate=200

# Check status
dmesg | tail -5

# Check GPIO state from sysfs
cat /sys/class/gpio/gpio18/value

# Unload (LED stops)
sudo rmmod gpio_led
dmesg | tail -5
```

## Imp points
1. gpio_request reserves the GPIO exclusively — prevents conflicts
2. Timer callback runs in softirq context — cannot sleep (no mutex, no schedule())
3. del_timer_sync waits for running callback — prevents use-after-free
4. module_param allows runtime configuration without recompiling
5. This HAL pattern is identical to how ONU firmware controls
   status LEDs — gpio-econet.ko on EN7528HU
