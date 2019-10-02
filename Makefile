KERNEL_VERSION=$(shell uname -r)
#KERNEL_VERSION=5.2.13-arch1-1-ARCH/
obj-m += nct6795_led.o
all:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) clean
install:
	cp nct6795_led.ko /lib/modules/$(KERNEL_VERSION)/extramodules/nct6795_led.ko
