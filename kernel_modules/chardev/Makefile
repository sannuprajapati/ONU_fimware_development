obj-m += chardev.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod chardev.ko

unload:
	sudo rmmod chardev

test:
	@echo "=== Writing to /dev/mydev ==="
	echo "Hello from ONU driver" | sudo tee /dev/mydev
	@echo "=== Reading from /dev/mydev ==="
	cat /dev/mydev
	@echo "=== Kernel log ==="
	dmesg | tail -10
