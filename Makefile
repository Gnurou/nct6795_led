KERNEL_VERSION=$(shell uname -r)
obj-m += nct6795_led.o

all:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) clean

install:
	mkdir -p /lib/modules/$(KERNEL_VERSION)/extramodules/
	cp nct6795_led.ko /lib/modules/$(KERNEL_VERSION)/extramodules/
