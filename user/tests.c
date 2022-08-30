#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include "lib/ioctl.h"

#define DATA "teststring\n"
#define SIZE strlen(DATA)
#define THREADS 10

void * the_thread(void* path){
        int ret;
        char* device;
        int fd;
        char buff[50];
        pthread_t self;

        device = (char*)path;

        fd = open(device,O_RDWR);
        if(fd == -1) {
                printf("open error on device %s\n",device);
                return NULL;
        }

		self = pthread_self();
        sprintf(buff, "thread\n", self);

        for(int i=0;i<100;i++) {
        	ret = write(fd,buff,strlen(buff));
                printf("Thread %d write ret %d\n", self, ret);
        	sleep(1);
     	}
        return NULL;
}

void * read_thread(void* path){
        int ret;
        char* device;
        int fd;
        char buff[50];
        pthread_t self;

        device = (char*)path;

        fd = open(device,O_RDWR);
        if(fd == -1) {
                printf("open error on device %s\n",device);
                return NULL;
        }

        self = pthread_self();

        for(int i=0;i<100;i++) {
                ret = read(fd, buff, 7);
                if(ret == 0) {
                        strcpy(buff, "");
                }
                printf("Thread %d read ret %d-%s\n", self, ret, buff);
                sleep(1);
        }
        return NULL;
}



int main(int argc, char** argv){
        int fd;
        int ret;
        int number;
        pthread_t tid;
        char buff[50];

        fd = open("./test", O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }

        /*
        number = 0;
        ioctl(fd,CHG_PRT,(int32_t*) &number);

        ret = write(fd, DATA, SIZE);
        printf("Write 1 %d\n", ret);

        read(fd, buff, SIZE);

        if(strcmp(buff, DATA)) {
        	printf("Test failed with data:%s:%s\n", DATA, buff);
        } else {
        	printf("Test 1 passed\n");
        }

        number = 1;
        ioctl(fd,CHG_PRT,(int32_t*) &number);
        write(fd,DATA,SIZE);

        read(fd, buff, SIZE);

        if(strcmp(buff, DATA)) {
        	printf("Test failed with data:%s:%s\n", DATA, buff);
        } else {
        	printf("Test 2 passed\n");
        }

        
        number = 0;
        ioctl(fd,CHG_BLK,(int32_t*) &number);
        //write(fd,DATA,SIZE);
        char buff2[50];

        read(fd, buff2, SIZE);

        if(strlen(buff2) != 0) {
                printf("Test failed with data:%s:%s\n", DATA, buff);
        } else {
                printf("Test 3 passed\n");
        }
        */
        // Return state back to normal
        
        number = 0;
        ioctl(fd,CHG_PRT,(int32_t*) &number);

        //Multi-thread tests
        for(int i=0;i<THREADS;i++) {
	        pthread_create(&tid,NULL,the_thread,"./test");
                pthread_create(&tid,NULL,read_thread,"./test");
        }
        
        pause();
        
        
        return 0;
}