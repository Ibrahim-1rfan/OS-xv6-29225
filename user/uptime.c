#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc != 1) {
        fprintf(2, "Usage: uptime\n");
        exit(1);
    }


    uint64 ticks = uptime(); //uptime system call
    int tick = (int)ticks;
    printf("uptime in ticks: %d ticks\n", tick);

    exit(0);
}
