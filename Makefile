KERNEL_VERSION=$(shell uname -r)
obj-m += leds-nct6795d.o

all:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD) clean

install:
	mkdir -p /lib/modules/$(KERNEL_VERSION)/extramodules/
	cp leds-nct6795d.ko /lib/modules/$(KERNEL_VERSION)/extramodules/

compile_commands.json: clean
	bear make
