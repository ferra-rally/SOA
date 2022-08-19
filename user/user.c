#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define DATA "grrr\n"
#define SIZE strlen(DATA)

#define CHG_PRT 0

int main(int argc, char** argv){
        int fd;
        int number;

        fd = open("./test", O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }

        number = 0;
        ioctl(fd,CHG_PRT,(int32_t*) &number);
        write(fd,DATA,SIZE);;

        number = 1;
        ioctl(fd,CHG_PRT,(int32_t*) &number);
        write(fd,DATA,SIZE);

        // Return state back to normal
        number = 0;
        ioctl(fd,CHG_PRT,(int32_t*) &number);
        return 0;
}