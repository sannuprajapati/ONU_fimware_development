obj-m += proc_module.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod proc_module.ko

unload:
	sudo rmmod proc_module

test:
	@echo "=== Status ==="
	cat /proc/mydriver/status
	@echo ""
	@echo "=== Stats ==="
	cat /proc/mydriver/stats
	@echo ""
	@echo "=== Advancing state ==="
	echo "init"  | sudo tee /proc/mydriver/control
	echo "start" | sudo tee /proc/mydriver/control
	echo "next"  | sudo tee /proc/mydriver/control
	echo "next"  | sudo tee /proc/mydriver/control
	cat /proc/mydriver/status
	@echo ""
	@echo "=== Stats after writes ==="
	cat /proc/mydriver/stats

log:
	dmesg | tail -15
