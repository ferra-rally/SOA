#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "lib/ioctl.h"

int main(int argc, char** argv){
	char *path;
	char *command;
	char *value;
	int fd;
    int cmd;
    int val;

	if(argc < 2) {
		printf("Invalid number of arguments\n");
		return -1;
	}

	path = argv[1];
	command = argv[2];
	value = argv[3];

	if(!strcmp("help", path)) {
		printf("Command list\nhelp: display commands\npriority <value> : change priority of node\nenabled <value>: enable and disable node\n");
		return 0;
	}

	fd = open("./test", O_RDWR);
    if(fd == -1) {
            printf("open error on device\n");
            return -1;
    }

    val = atoi(value);
	if(!strcmp("priority", command)) {
		cmd = CHG_PRT;
	} else if(!strcmp("enable", command)) {
		cmd = CHG_ENB_DIS;
	} else if(!strcmp("timeout", command)) {
		cmd = CHG_TO;
	} else if(!strcmp("block", command)) {
		cmd = CHG_BLK;
	} else {
		printf("Invalid command\n");
		return 0;
	}

	int ret = ioctl(fd,cmd,(int32_t*) &val);
	if(ret != 0) {
		printf("Error in ioctl with %d:%d\n", cmd, val);
		return -1;
	}

    return 0;
}