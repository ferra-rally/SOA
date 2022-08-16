#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define CHG_PRT 0

int main(int argc, char** argv){
        int fd;
        int number;

        number = 1;

        sleep(1);

        fd = open("./test", O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }

        printf("device successfully opened\n");
        ioctl(fd,CHG_PRT,(int32_t*) &number);

        return 0;
}