# sleep(easy)
Implement the UNIX program sleep for xv6; your sleep should pause for a user-specified number of ticks. A tick is a notion of time defined by the xv6 kernel, namely the time between two interrupts from the timer chip. Your solution should be in the file user/sleep.c.

实现sleep功能，睡眠用户指定长度的ticks。tick是xv6内核定义的时间概念，即来自定时器芯片的两次中断之间的时间。在/user/sleep.c中实现。

**Some hints:**

- Before you start coding, read Chapter 1 of the xv6 book.
- Look at some of the other programs in user/ (e.g., user/echo.c, user/grep.c, and user/rm.c) to see how you can obtain the command-line arguments passed to a program.
- If the user forgets to pass an argument, sleep should print an error message.用户忘记输入参数，需要报错
- The command-line argument is passed as a string; you can convert it to an integer using atoi (see user/ulib.c).命令行参数作为字符串传入，使用atoi转换为整型
- Use the system call sleep.用sleep系统调用。
- See kernel/sysproc.c for the xv6 kernel code that implements the sleep system call (look for sys_sleep), user/user.h for the C definition of sleep callable from a user program, and user/usys.S for the assembler code that jumps from user code into the kernel for sleep.
- Make sure main calls exit() in order to exit your program.
- Add your sleep program to UPROGS in Makefile; once you've done that, make qemu will compile your program and you'll be able to run it from the xv6 shell.
- Look at Kernighan and Ritchie's book The C programming language (second edition) (K&R) to learn about C.

**解决思路**
判断参数数量，判断用户是否正确输入命令，处理第二个参数转化为int，调用系统调用Sleep。
**具体实现**
较简单，详见代码。
# pingpong(easy)
Write a program that uses UNIX system calls to ''ping-pong'' a byte between two processes over a pair of pipes, one for each direction. The parent should send a byte to the child; the child should print "<pid>: received ping", where <pid> is its process ID, write the byte on the pipe to the parent, and exit; the parent should read the byte from the child, print "<pid>: received pong", and exit. Your solution should be in the file user/pingpong.c.
编写一个程序，使用UNIX系统调用在一对管道上的两个进程之间“pingpong”一个字节，每个方向一个。父级向子级发送一个字节；子级打印“＜pid＞：received ping”，其中＜pid＞是其进程ID，将管道上的字节写入父级，然后退出；父级应该读取子级的字节，打印“＜pid＞：received-pong”，然后退出。在文件user/pingpong.c中实现。

**Some hints:**

- Use pipe to create a pipe.
- Use fork to create a child.
- Use read to read from the pipe, and write to write to the pipe.
- Use getpid to find the process ID of the calling process.
- Add the program to UPROGS in Makefile.
- User programs on xv6 have a limited set of library functions available to them. You can see the list in user/user.h; the source (other than for system calls) is in user/ulib.c, user/printf.c, and user/umalloc.c.

**解决思路**
创建管道，创建子进程。
在父进程先写后读然后打印收到，在子进程先读，打印收到后写。
**具体实现**
```c
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
```
# primes (moderate)/(hard)
Write a concurrent version of prime sieve using pipes. This idea is due to Doug McIlroy, inventor of Unix pipes. The picture halfway down [this page](https://swtch.com/~rsc/thread/) and the surrounding text explain how to do it. Your solution should be in the file user/primes.c.
使用管道编写prime sieve的并发版本。这个想法要归功于Unix管道的发明者Doug McIlroy。这个页面中间的图片和周围的文字解释了如何做到这一点。你的解决方案应该在user/primes.c文件中。
Your goal is to use pipe and fork to set up the pipeline. The first process feeds the numbers 2 through 35 into the pipeline. For each prime number, you will arrange to create one process that reads from its left neighbor over a pipe and writes to its right neighbor over another pipe. Since xv6 has limited number of file descriptors and processes, the first process can stop at 35.
您的目标是使用pipe和fork设置管道。第一个过程将数字2到35输入到管道中。对于每个素数，您将安排创建一个进程，该进程通过一个管道从其左邻居读取，并通过另一个管道向其右邻居写入。由于xv6具有有限数量的文件描述符和进程，因此第一个进程可以在35处停止。

**Some hints:**

- Be careful to close file descriptors that a process doesn't need, because otherwise your program will run xv6 out of resources before the first process reaches 35.
- Once the first process reaches 35, it should wait until the entire pipeline terminates, including all children, grandchildren, &c. Thus the main primes process should only exit after all the output has been printed, and after all the other primes processes have exited.
- Hint: read returns zero when the write-side of a pipe is closed.
- It's simplest to directly write 32-bit (4-byte) ints to the pipes, rather than using formatted ASCII I/O.
- You should create the processes in the pipeline only as they are needed.
- Add the program to UPROGS in Makefile.

**解决思路**
采用递归，先尝试读一个，如果读不到说明到终点了exit。否则打印该数并fork一个新进程，在子进程中递归调用，父进程向筛选写。时刻注意关闭不用的pipe。
```c
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
```
# find (moderate)
Write a simple version of the UNIX find program: find all the files in a directory tree with a specific name. Your solution should be in the file user/find.c.

**Some hints:**

- Look at user/ls.c to see how to read directories.
- Use recursion to allow find to descend into sub-directories.
- Don't recurse into "." and "..".
- Changes to the file system persist across runs of qemu; to get a clean file system run make clean and then make qemu.
- You'll need to use C strings. Have a look at K&R (the C book), for example Section 5.5.
- Note that == does not compare strings like in Python. Use strcmp() instead.
- Add the program to UPROGS in Makefile.

**解决思路**
参考ls实现。如果path是文件报错，是目录，那么遍历目录，对目录的每个项，如果是文件判断是都是target，如果是目录，递归。
**具体实现**
主要看靠user/ls.c,详见具体代码，不再赘述。
# xargs (moderate)
Write a simple version of the UNIX xargs program: read lines from the standard input and run a command for each line, supplying the line as arguments to the command. Your solution should be in the file user/xargs.c.
The following example illustrates xarg's behavior:
```sh
    $ echo hello too | xargs echo bye
    bye hello too
    $
```
Note that the command here is "echo bye" and the additional arguments are "hello too", making the command "echo bye hello too", which outputs "bye hello too".

**Some hints:**
- Use fork and exec to invoke the command on each line of input. Use wait in the parent to wait for the child to complete the command.
- To read individual lines of input, read a character at a time until a newline ('\n') appears.
- kernel/param.h declares MAXARG, which may be useful if you need to declare an argv array.
- Add the program to UPROGS in Makefile.
- Changes to the file system persist across runs of qemu; to get a clean file system run make clean and then make qemu.

**解决思路**
使用fork起一个子进程，在子进程中用exec执行相应的命令。父进程wait。对标准输入每次读一个char，若读到\n需要执行命令。注意在执行xargs这个命令行的时候，最后肯定要按一个回车，这时标准输入最后会有一个回车，所以在EOF前是会有一个回车的！！！

