A=$(sudo cat /sys/module/the_hlm/parameters/major_number)

mknod $1 c $A $2