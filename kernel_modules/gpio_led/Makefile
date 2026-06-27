obj-m += gpio_led.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod gpio_led.ko gpio_pin=18 blink_rate=500

unload:
	sudo rmmod gpio_led

# Load with custom parameters
load-fast:
	sudo insmod gpio_led.ko gpio_pin=18 blink_rate=200

log:
	dmesg | tail -10
