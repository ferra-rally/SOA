obj-m += the_hlm.o 
the_hlm-objs += hlm.o

compile:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
mount:
	sudo insmod the_hlm.ko
	make -C ./user/ node
unmount:
	sudo rmmod the_hlm.ko

reload:
	sudo rmmod the_hlm.ko
	sudo insmod the_hlm.ko
	make -C ./user/ node
