#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main()
{
  printf("I/O-bound process started (pid: %d)\n", getpid());
  
  int i;
  for(i = 0; i < 2; i++) {
    // Do small amount of work
    int j;
    for(j = 0; j < 100; j++); 
    
    printf("I/O-bound %d: iteration %d, sleeping...\n", getpid(), i);
    pause(10); // Sleep for 10 ticks - should work now
  }
  
  printf("I/O-bound process %d finished\n", getpid());
  exit(0);
}
