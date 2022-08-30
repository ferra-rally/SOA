A = $(sudo cat /sys/module/the_hlm/parameters/major_number)

sudo mknod $(1) c $A $(2)
sudo chown $(USER) $(1) 
