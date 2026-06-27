/*
 * gpio_led.c - GPIO LED Blink Driver
 *
 * Demonstrates:
 *   - GPIO HAL API (gpio_request, gpio_set_value, gpio_free)
 *   - Kernel timer (timer_list, mod_timer, del_timer_sync)
 *   - __init / __exit module lifecycle
 *   - Hardware control from kernel space
 *
 * Relevance to ONU firmware (EN7528HU TCLinux):
 *   gpio-econet.ko uses the same GPIO HAL API to control
 *   ONU status LEDs (PON registered, LOS alarm, power).
 *   The GPIO HAL abstracts EN7528 GPIO register addresses
 *   from the code above it.
 *
 * Hardware:
 *   Connect LED + resistor between GPIO_PIN and GND.
 *   On Raspberry Pi: GPIO 18 = physical pin 12.
 *   Change GPIO_PIN to match your board.
 *
 * Test:
 *   sudo insmod gpio_led.ko
 *   dmesg | tail -5       (LED starts blinking)
 *   sudo rmmod gpio_led   (LED stops)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/moduleparam.h>

/* Change this to match your GPIO pin */
static int gpio_pin   = 18;
static int blink_rate = 500;  /* milliseconds */

module_param(gpio_pin,   int, 0444);
module_param(blink_rate, int, 0644);
MODULE_PARM_DESC(gpio_pin,   "GPIO pin number (default 18)");
MODULE_PARM_DESC(blink_rate, "Blink rate in ms (default 500)");

static struct timer_list blink_timer;
static int led_state = 0;

/*
 * blink_handler - called by kernel timer every blink_rate ms
 *
 * This runs in softirq context — cannot sleep here.
 * gpio_set_value is safe in interrupt context.
 * mod_timer reschedules the timer for next blink.
 */
static void blink_handler(struct timer_list *t)
{
    led_state = !led_state;
    gpio_set_value(gpio_pin, led_state);

    pr_debug("gpio_led: GPIO %d -> %s\n",
             gpio_pin, led_state ? "HIGH" : "LOW");

    /* reschedule — keeps blinking */
    mod_timer(&blink_timer,
              jiffies + msecs_to_jiffies(blink_rate));
}

static int __init gpio_led_init(void)
{
    int ret;

    /* Step 1: request GPIO — reserves it exclusively */
    ret = gpio_request(gpio_pin, "led_blink");
    if (ret) {
        pr_err("gpio_led: GPIO %d request failed (%d)\n",
               gpio_pin, ret);
        return ret;
    }

    /* Step 2: set direction to output, initial state LOW */
    ret = gpio_direction_output(gpio_pin, 0);
    if (ret) {
        pr_err("gpio_led: set direction failed (%d)\n", ret);
        gpio_free(gpio_pin);
        return ret;
    }

    /* Step 3: setup kernel timer */
    timer_setup(&blink_timer, blink_handler, 0);

    /* Step 4: schedule first callback */
    mod_timer(&blink_timer,
              jiffies + msecs_to_jiffies(blink_rate));

    pr_info("gpio_led: loaded — GPIO %d blinking at %dms\n",
            gpio_pin, blink_rate);
    return 0;
}

static void __exit gpio_led_exit(void)
{
    /* stop the timer — del_timer_sync waits for
       any running callback to complete */
    del_timer_sync(&blink_timer);

    /* turn LED off */
    gpio_set_value(gpio_pin, 0);

    /* release GPIO back to the system */
    gpio_free(gpio_pin);

    pr_info("gpio_led: unloaded — GPIO %d released\n",
            gpio_pin);
}

module_init(gpio_led_init);
module_exit(gpio_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ONU Firmware Engineer");
MODULE_DESCRIPTION("GPIO LED Blink — models gpio-econet.ko HAL pattern");
MODULE_VERSION("1.0");
