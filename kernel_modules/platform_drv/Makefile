obj-m += platform_drv.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod platform_drv.ko

unload:
	sudo rmmod platform_drv

status:
	@cat /proc/myplatform_status 2>/dev/null || \
	echo "Module not loaded or no hardware matched"

log:
	dmesg | tail -20
