#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include "lib/ioctl.h"

#define DATA "teststring"
#define DATA2 "teststringteststring"
#define SIZE strlen(DATA)
#define SIZE2 strlen(DATA2)
#define THREADS 10
#define EXECUTIONS 100
#define TESTS 20

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

        for(int i=0;i<EXECUTIONS;i++) {
        	ret = write(fd,buff,strlen(buff));
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

        for(int i=0;i<EXECUTIONS;i++) {
                ret = read(fd, buff, 7);
                sleep(1);
        }
        return NULL;
}



int main(int argc, char** argv){
        int fd;
        int ret;
        int number;
        clock_t avg;
        clock_t start;
        clock_t end;
        pthread_t tid;
        char buff[4096];

        fd = open("./test", O_RDWR);
        if(fd == -1) {
                printf("open error on device\n");
                return -1;
        }
        
        for(int prt = 0; prt < 2; prt ++) {
                number = prt;
                ioctl(fd,CHG_PRT,(int32_t*) &number);

                for(int i = 0; i < TESTS; i++) {
                        start = clock();
                        write(fd, DATA, SIZE);
                        end = clock();

                        avg += end - start;
                }

                printf("Single write in single block prt %d: %lf\n", prt, ((double)(avg/TESTS))/CLOCKS_PER_SEC);
                avg = 0;
                for(int i = 0; i < TESTS; i++) {
                        start = clock();
                        read(fd, buff, SIZE);
                        end = clock();

                        avg += end - start;
                }

                printf("Single read in single block prt %d: %lf\n", prt, ((double)(avg))/CLOCKS_PER_SEC);

                avg = 0;

                for(int i = 0; i < TESTS; i++) {
                        start = clock();
                        write(fd, DATA2, SIZE2);
                        end = clock();

                        avg += end - start;
                }

                printf("Single write in multiple blocks prt %d: %lf\n", prt, ((double)(avg))/CLOCKS_PER_SEC);
                
                avg = 0;
                for(int i = 0; i < TESTS; i++) {
                        start = clock();
                        read(fd, buff, SIZE2);
                        end = clock();

                        avg += end - start;
                }

                printf("Single read in multiple blocks prt %d: %lf\n", prt, ((double)(avg))/CLOCKS_PER_SEC);
        }
        
        //Multi-thread tests
        for(int i=0;i<THREADS;i++) {
	        pthread_create(&tid,NULL,the_thread,"./test");
                pthread_create(&tid,NULL,read_thread,"./test");
        }

        for(int prt = 0; prt < 2; prt ++) {
                number = prt;
                ioctl(fd,CHG_PRT,(int32_t*) &number);
                sleep(2);
               for(int i = 0; i < TESTS; i++) {
                        start = clock();
                        write(fd, DATA, SIZE);
                        end = clock();

                        avg += end - start;
                }

                printf("Concurrent write in single block prt %d: %lf\n", prt, ((double)(avg))/CLOCKS_PER_SEC);

                avg = 0;
                for(int i = 0; i < TESTS; i++) {
                        start = clock();
                        read(fd, buff, SIZE);
                        end = clock();

                        avg += end - start;
                }

                printf("Concurrent read in single block prt %d: %lf\n", prt, ((double)(avg))/CLOCKS_PER_SEC);
        }
        
        return 0;
}