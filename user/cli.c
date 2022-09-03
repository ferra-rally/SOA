#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "lib/ioctl.h"

int fd;
char input[50];

int read_command() {
        int nbytes;
        int offset;
        int ret;
        char out[500] = "";

        printf("read>> num bytes:");
        scanf("%d", &nbytes);
        printf("read>> offset: ");
        scanf("%d", &offset);

        ret = pread(fd, out, nbytes, offset);
        if(ret < 0) {
                printf("Error while reading\n");
                return -1;
        }

        printf("out %d>> %s\n", ret, out);

        return 0;
}

int write_command() {
        int len;
        int ret;
        char in[500];

        printf("write>> ");
        scanf("%s", in);
        len = strlen(in);
        
        ret = write(fd, in, len);
        if(ret == -28) {
                printf("Not enough space in the device\n");
                return -1;
        }

        printf("delivered %d bytes\n", ret);

        return 0;
}

int ioctl_command() {
        char command[25];
        int value;
        int ret;
        int cmd;

        printf("ioctl>> command(timeout, enable, priority, block):  ");
        scanf("%s", command);

        printf("ioctl>> value: ");
        scanf("%d", &value);

        if(!strcmp("priority", command)) {
                cmd = CHG_PRT;
        } else if(!strcmp("enable", command)) {
                cmd = CHG_ENB_DIS;
        } else if(!strcmp("timeout", command)) {
                cmd = CHG_TIMEOUT;
        } else if(!strcmp("block", command)) {
                cmd = CHG_BLK;
        } else {
                printf("Invalid command\n");
                return -1;
        }

        ret = ioctl(fd,cmd,(int32_t*) &value);
        if(ret != 0) {
                printf("Error in ioctl with %d:%d\n", cmd, value);
                return -1;
        }

        printf("Change done\n");

        return 0;
}

int main(int argc, char** argv){
        int number;
        char command[50];
        char input[50];

        if(argc != 2) {
                printf("Invalid number of parameters, usage: cli <filename>\n");
        }

        fd = open(argv[1], O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }

        printf("HLM cli, press help to list all commands\n");

        while(1) {
                printf(">>> ");
                scanf("%s", command);

                if(!strcmp(command, "help")) {
                        printf("List of commands\nhelp: displays commands\nwrite <string>: write the string in the device\nread: read number of data from the flow\nioctl: can change the device behaviour\n");
                } else if(!strcmp(command, "read")) {
                        read_command();
                } else if(!strcmp(command, "write")) {
                        write_command();
                } else if(!strcmp(command, "ioctl")) {
                        ioctl_command();
                }else {
                        printf("Invalid command, type help for list of commands\n");
                }
        }

        return 0;
}