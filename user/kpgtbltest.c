#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  kpgtbl();  // This will call our vmprint function
  exit(0);
}
