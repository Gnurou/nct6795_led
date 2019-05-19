obj-m += nct6795_led.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
install:
	cp nct6795_led.ko /lib/modules/`uname -r`/extramodules/nct6795_led.ko
