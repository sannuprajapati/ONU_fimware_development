KDIR := /lib/modules/$(shell uname -r)/build
MODULES := chardev gpio_led i2c_client platform_drv proc_module

.PHONY: all clean load-all unload-all demo

all:
	@for m in $(MODULES); do \
		echo "=== Building $$m ==="; \
		$(MAKE) -C $$m; \
	done

clean:
	@for m in $(MODULES); do \
		echo "=== Cleaning $$m ==="; \
		$(MAKE) -C $$m clean; \
	done

# Load modules that don't need hardware
load-all:
	@echo "Loading chardev..."
	-sudo insmod chardev/chardev.ko
	@echo "Loading proc_module..."
	-sudo insmod proc_module/proc_module.ko
	@echo ""
	@echo "Loaded. Check: dmesg | tail -10"

unload-all:
	-sudo rmmod proc_module
	-sudo rmmod chardev
	@echo "All modules unloaded"

# Live demo sequence for interviews
demo:
	@echo ""
	@echo "========================================"
	@echo "  KERNEL DRIVER DEMO"
	@echo "========================================"
	@echo ""
	@echo "--- 1. Character Device Driver ---"
	-sudo insmod chardev/chardev.ko
	@sleep 1
	@echo "hello from interview" | sudo tee /dev/mydev
	@cat /dev/mydev
	@echo ""
	@echo "--- 2. Procfs State Machine ---"
	-sudo insmod proc_module/proc_module.ko
	@sleep 1
	@cat /proc/mydriver/status
	@echo "init"  | sudo tee /proc/mydriver/control
	@echo "start" | sudo tee /proc/mydriver/control
	@echo "next"  | sudo tee /proc/mydriver/control
	@echo "next"  | sudo tee /proc/mydriver/control
	@cat /proc/mydriver/status
	@echo ""
	@echo "--- Kernel logs ---"
	@dmesg | tail -20
	@echo ""
	@echo "--- Cleanup ---"
	-sudo rmmod proc_module
	-sudo rmmod chardev
	@echo "Demo complete"
