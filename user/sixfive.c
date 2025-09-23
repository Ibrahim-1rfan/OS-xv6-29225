#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(2, "Usage: sixfive <filename1> [filename2] ...\n");
        exit(1);
    }
    
    int i = 1;  // Start from 1 (argv[0] is program name)
    while(i < argc) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(2, "sixfive: cannot open %s\n", argv[i]);
            i = i + 1;
            continue;  // Skip to next file instead of exiting
        }
        
        char buf;
        char num_buf[32];
        int num_index = 0;

        while (read(fd, &buf, 1) > 0) {
            if (buf >= '0' && buf <= '9') {
                if (num_index < sizeof(num_buf) - 1) {
                    num_buf[num_index++] = buf;
                }
            } else {
                // Any non-digit is a separator
                if (num_index > 0) {
                    num_buf[num_index] = '\0';
                    int number = atoi(num_buf);
                    if (number % 5 == 0 || number % 6 == 0) {
                        printf("%d\n", number);
                    }
                    num_index = 0;
                }
            }
        }

        // Handle last number in the file
        if (num_index > 0) {
            num_buf[num_index] = '\0';
            int number = atoi(num_buf);
            if (number % 5 == 0 || number % 6 == 0) {
                printf("%d\n", number);
            }
        }

        close(fd);
        i = i + 1;
    }
    
    exit(0);
}
