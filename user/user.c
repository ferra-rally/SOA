#include <unistd.h>

#define IOCTL 156

int main(int argc, char** argv){
        syscall(IOCTL,1);
}