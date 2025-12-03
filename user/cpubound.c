#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main()
{
  printf("CPU-bound process started (pid: %d)\n", getpid());
  
  volatile unsigned long x = 0;
  while(1) {
    x += 1;
    // Print every 10 million iterations to show progress
    if(x % 10000000 == 0) {
      printf("CPU-bound %d: iteration %ld\n", getpid(), x);
    }
  }
  
  exit(0);
}
