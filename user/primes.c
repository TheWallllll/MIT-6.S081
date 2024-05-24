#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void newChild(int p[2]) {
  int n;
  int newp[2];
  pipe(newp);

  close(p[1]);
  if (read(p[0], &n, sizeof(int)) == 0)
    exit(0);
  printf("prime %d\n", n);

  if (fork() == 0) {
    newChild(newp);
  }
  else {
    close(newp[0]);
    int num;
    while (read(p[0], &num, sizeof(int)) == 4) {
      if (num % n)
        write(newp[1], &num, sizeof(int));
    }
    close(p[0]);
    close(newp[1]);
    wait(0);
  }
  exit(0);
}

int main(int argc, char* argv[]) {
  int p[2];
  pipe(p);

  if (fork() == 0) {
    newChild(p);
  }
  else {
    close(p[0]);
    for (int i = 2;i <= 35;++i) {
      write(p[1], &i, sizeof(int));
    }
    close(p[1]);
    wait(0);
  }
  exit(0);
}