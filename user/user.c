#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define DATA "graaaa"
#define SIZE strlen(DATA)

#define CHG_PRT 0

int main(int argc, char** argv){
        int fd;
        int number;
        char buff[20];

        fd = open("./test", O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }

        number = 1;
        ioctl(fd,CHG_PRT,(int32_t*) &number);
        printf("Writing %d bytes\n", strlen(DATA));
        int ret = write(fd, DATA, SIZE);
        printf("write ret: %d\n", ret);

        read(fd, buff, 3);
        printf("Reading %d bytes: ---\n%s\n---\n", strlen(buff), buff);

        return 0;
}