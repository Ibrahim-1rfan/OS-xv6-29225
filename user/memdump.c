#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data);

int
main(int argc, char *argv[])
{
  if(argc == 1){
    printf("Example 1:\n");
    int a[2] = { 61810, 2025 };
    memdump("ii", (char*) a);
    
    printf("Example 2:\n");
    memdump("S", "a string");
    
    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *) &s);

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;
    
    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");
    
    printf("Example 4:\n");
    memdump("pihcS", (char*) &example);
    
    printf("Example 5:\n");
    memdump("sccccc", (char*) &example);
  } else if(argc == 2){
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while(n < sizeof(data)){
      int nn = read(0, data + n, sizeof(data) - n);
      if(nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data)
{
  // Your code here.
      char *current_data = data;
    
    for (int i = 0; fmt[i] != '\0'; i++) {
        switch (fmt[i]) {
            case 'i': { // 32-bit integer (4 bytes)
                int *int_ptr = (int*)current_data;
                printf("%d\n", *int_ptr);
                current_data += sizeof(int);
                break;
            }
            case 'p': { // 64-bit pointer (8 bytes) in hex
                uint64 *ptr_ptr = (uint64*)current_data;
                printf("%lx\n", *ptr_ptr);
                current_data += sizeof(uint64);
                break;
            }
            case 'h': { // 16-bit integer (2 bytes)
                short *short_ptr = (short*)current_data;
                printf("%d\n", *short_ptr);
                current_data += sizeof(short);
                break;
            }
            case 'c': { // 8-bit ASCII character (1 byte)
                char *char_ptr = (char*)current_data;
                printf("%c\n", *char_ptr);
                current_data += sizeof(char);
                break;
            }
            case 's': { // 64-bit pointer to C string
                char **str_ptr_ptr = (char**)current_data;
                char *str_ptr = *str_ptr_ptr;
                printf("%s\n", str_ptr);
                current_data += sizeof(char*);
                break;
            }
            case 'S': { // null-terminated C string in-place
                char *str_ptr = current_data;
                printf("%s\n", str_ptr);
                // Move past the null terminator
                while (*current_data != '\0') {
                    current_data++;
                }
                current_data++; // Skip the null terminator
                break;
            }
            default: {
                printf("Unknown format character: %c\n", fmt[i]);
                return;
            }
        }
    }
}
