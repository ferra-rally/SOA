obj-m += hlm.o

compile:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
mount:
	sudo insmod hlm.ko

unmount:
	sudo rmmod hlm.ko
