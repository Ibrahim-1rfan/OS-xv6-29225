#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main()
{
  printf("Yielding process started (pid: %d)\n", getpid());
  
  int i;
  for(i = 0; i < 20; i++) {
    printf("Yielding %d: iteration %d\n", getpid(), i);
    yield();
  }
  
  printf("Yielding process %d finished\n", getpid());
  exit(0);
}
