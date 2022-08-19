A = $(shell sudo cat /sys/module/the_hlm/parameters/major_number)

all:
	gcc user.c
node:
	sudo rm ./test
	sudo mknod test c $(A) 1
	sudo chown $(USER) test
