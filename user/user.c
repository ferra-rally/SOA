#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "lib/ioctl.h"

#define DATA "abcdef"
#define SIZE strlen(DATA)

int main(int argc, char** argv){
        int fd;
        int number;
        int ret;
        char buff[20];
        char buff2[20];

        fd = open("./test", O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }

        number = 1;
        ioctl(fd,CHG_PRT,(int32_t*) &number);

        number = 0;
        ioctl(fd,CHG_BLK,(int32_t*) &number);

        printf("Writing %d bytes\n", 2 * strlen(DATA));
        ret = write(fd, DATA, SIZE);
        printf("write ret: %d\n", ret);
        ret = write(fd, DATA, SIZE);
        printf("write ret: %d\n", ret);

        pread(fd, buff, 5, 0);
        printf("Reading 1 %d bytes: ---\n%s\n---\n", strlen(buff), buff);
        ret = read(fd, buff2, 20);
        printf("Reading 2 %d bytes: ---%s---\n", ret, buff2);

        /*
        printf("Writing %d bytes\n", strlen(DATA));
        ret = write(fd, DATA, SIZE);
        printf("write ret: %d\n", ret);
        read(fd, buff, 3);
        printf("Reading %d bytes: ---\n%s\n---\n", strlen(buff), buff);

        read(fd, buff, 20);
        printf("Reading %d bytes: ---\n%s\n---\n", strlen(buff), buff);

        ret = write(fd, DATA, SIZE);
        printf("write ret: %d\n", ret);
        lseek(fd, 2, SEEK_SET);
        read(fd, buff, 20);
        printf("Reading %d bytes: ---\n%s\n---\n", strlen(buff), buff);
        */
        return 0;
}