#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    // Check if the correct number of arguments is provided
    if (argc != 2) {
        fprintf(2, "Usage: sleep <ticks>\n");
        exit(1);
    }

    // Convert the argument to an integer
    int ticks = atoi(argv[1]);

    // Call the pause system call
    pause(ticks);

    exit(0);
}
