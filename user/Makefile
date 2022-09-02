A = $(shell sudo cat /sys/module/the_hlm/parameters/major_number)

all:
	gcc user.c -o user
	gcc utility.c -o utility
	gcc tests.c -lpthread  -o tests
	gcc timing.c -lpthread -o timing
node:
	sudo rm ./test
	sudo mknod test c $(A) 1
	sudo chown $(USER) test
create_other_node:
	sudo mknod grrr c $(A) 2
	sudo chown $(USER) grrr 		
