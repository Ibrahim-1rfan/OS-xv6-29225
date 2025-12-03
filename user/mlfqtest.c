#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"


int
main(int argc, char *argv[])
{
  int pid;
  int i;
  int num_procs = 5;
  int child_pids[5];

  // 1. Create 5 CPU-bound processes
  for(i = 0; i < num_procs; i++) {
    pid = fork();
    if(pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    if(pid == 0) {
      // Child: CPU Bound loop
      volatile unsigned long x = 0;
      while(1) { x++; } // Spin forever
    } else {
      child_pids[i] = pid;
    }
  }

  // 2. Monitor Loop (The Parent)
  struct procinfo pi;
  for (i = 0; i < 21; i++) { // Run for 20 rounds (approx 2000 ticks)
    
    // Print the header
    printf("Round %d (ticks=%d) - Priorities: ", i, i*100);

    // Check each child
    for (int j = 0; j < num_procs; j++) {
      if (getprocinfo(child_pids[j], &pi) == 0) {
        printf("P%d:%d ", j, pi.qlevel);
      }
    }
    printf("\n");

    // Sleep for 100 ticks
    pause(100);
  }

  // 3. Cleanup
  for(i = 0; i < num_procs; i++) {
    kill(child_pids[i]);
  }
  
  // Wait for them to exit
  for(i = 0; i < num_procs; i++) {
    wait(0);
  }

  exit(0);
}

