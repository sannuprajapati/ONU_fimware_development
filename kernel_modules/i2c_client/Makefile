obj-m += i2c_client.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

# Test using i2c-stub (simulated I2C device, no real hardware needed)
test-stub:
	sudo modprobe i2c-stub chip_addr=0x48
	sudo insmod i2c_client.ko
	dmesg | tail -10

unload:
	sudo rmmod i2c_client

log:
	dmesg | tail -15
