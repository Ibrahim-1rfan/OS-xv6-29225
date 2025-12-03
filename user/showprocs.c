#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main()
{
  struct procinfo pi;
  int pid;
  
  printf("PID\tState\tQueue\tTicks\tName\n");
  printf("---\t-----\t-----\t-----\t----\n");
  
  // Check PIDs from 1 to 64 (typical xv6 range)
  for(pid = 1; pid < 64; pid++) {
    if(getprocinfo(pid, &pi) == 0) {
      char *state;
      switch(pi.state) {
        case 0: state = "UNUSED"; break;
        case 1: state = "USED"; break;
        case 2: state = "SLEEP"; break;
        case 3: state = "RUNNABLE"; break;
        case 4: state = "RUNNING"; break;
        case 5: state = "ZOMBIE"; break;
        default: state = "?"; break;
      }
      printf("%d\t%s\t%d\t%d\t%s\n", 
             pi.pid, state, pi.qlevel, pi.qticks, pi.name);
    }
  }
  
  exit(0);
}
