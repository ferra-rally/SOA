obj-m += the_hlm.o 
the_hlm-objs += hlm.o lib/scth.o

A = $(shell sudo cat /sys/module/the_usctm/parameters/sys_call_table_address)

compile:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
mount:
	sudo insmod the_hlm.ko the_syscall_table=$(A)

unmount:
	sudo rmmod the_hlm.ko

reload:
	sudo rmmod the_hlm.ko
	sudo insmod the_hlm.ko the_syscall_table=$(A)
