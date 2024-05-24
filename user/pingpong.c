#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  int p[2];
  pipe(p);

  int pid = fork();
  if (pid == 0) {
    pid = getpid();
    char buf[1];

    read(p[0], buf, 1);
    close(p[0]);
    printf("%d: received ping\n", pid);
    write(p[1], buf, 1);
    close(p[1]);
  }else {
    pid = getpid();
    char buf[1];

    write(p[1], "a", 1);
    close(p[1]);
    read(p[0], buf, 1);
    printf("%d: received pong\n", pid);
    close(p[0]);

    wait(0);
  }
  exit(0);
}