# Large files(moderate)

xv6中文件限制为268个块。一个xv6的inode包含12个直接的data block和一个间接的block，间接block包含了256（一个block的编号4字节）个data block。要求修改inode相关代码，让xv6支持最多65803个块，即11个直接的data block，一个一级索引的间接块和一个二级索引的简介块。主要工作是修改bmap()。

比较简单，不多赘述。

需要注意的点是：修改NDIRECT的定义，那么inode and dinode内的addrs[]都需要修改。

其次，尤其注意，需要修改最大文件大小。

# Symbolic links(moderate)

**your job**

> You will implement the symlink(char \*target, char \*path) system call, which creates a new symbolic link at path that refers to file named by target. For further information, see the man page symlink. To test, add symlinktest to the Makefile and run it. Your solution is complete when the tests produce the following output (including usertests succeeding).

Hints:

* First, create a new system call number for symlink, add an entry to user/usys.pl, user/user.h, and implement an empty sys\_symlink in kernel/sysfile.c.
* Add a new file type (T\_SYMLINK) to kernel/stat.h to represent a symbolic link.
* Add a new flag to kernel/fcntl.h, (O\_NOFOLLOW), that can be used with the open system call. Note that flags passed to open are combined using a bitwise OR operator, so your new flag should not overlap with any existing flags. This will let you compile user/symlinktest.c once you add it to the Makefile.
* Implement the symlink(target, path) system call to create a new symbolic link at path that refers to target. Note that target does not need to exist for the system call to succeed. You will need to choose somewhere to store the target path of a symbolic link, for example, in the inode's data blocks. symlink should return an integer representing success (0) or failure (-1) similar to link and unlink.
* Modify the open system call to handle the case where the path refers to a symbolic link. If the file does not exist, open must fail. When a process specifies O\_NOFOLLOW in the flags to open, open should open the symlink (and not follow the symbolic link).
* If the linked file is also a symbolic link, you must recursively follow it until a non-link file is reached. If the links form a cycle, you must return an error code. You may approximate this by returning an error code if the depth of links reaches some threshold (e.g., 10).
* Other system calls (e.g., link and unlink) must not follow symbolic links; these system calls operate on the symbolic link itself.
* You do not have to handle symbolic links to directories for this lab.

**analysis**

给xv6添加一个软链接系统调用。

软链接类似于windows中的快捷方式，对一个文件进行硬链接，那么对应inode的nlink会加1，两者的inode是同一个，删除一个文件，另一个文件不受影响，只是nlink-1。软链接是创建一个新的文件，有新的inode，不过他的类型是软链接文件，链接到对应的文件，用的是绝对路径，如果删除源文件，那么软链接失效，但是如果重新创建源文件，路径相同，那么软链接又会生效。

创建一个系统调用，在kernel/sysfile.c中实现它，成功返回0，失败返回-1，创建一个新的inode，将target写入inode的数据块中，在sys_open中注意对软链接文件的处理，链接的文件也可能是符号链接，需要递归跟随，直到不是链接文件位置，如果形成循环，那么需要返回错误，可以使用循环深度来处理，超过10就是循环链接。

**solution**

创建一个系统调用已经做过很多次不再赘述。

kernel/sysfile.c:

```c
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;

  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
    end_op();
    return -1;
  }

 
  if (writei(ip, 0, (uint64)target, 0, strlen(target)) < 0) {
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();

  return 0;
}
```

重点小心锁的处理，writei的使用，create的使用，create会返回一个新创建的inode，并且该inode被锁定，因此不需要在对他加锁。考虑下面的写法：

```c
  begin_op();
  if ((ip = namei(path)) == 0) {
    ip = create(path, T_SYMLINK, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
    //iunlock(ip);
  }
  //ilock(ip);
 
  if (writei(ip, 0, (uint64)target, 0, strlen(target)) < 0) {
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
```

这里如果注释了解锁加锁，那么多个进程一起，如果有两个进程同时创建一个软链接，那么第一个进程发现没有这个软链接文件，进入if，创建了inode，持有他的锁，最后释放，此时另一个进程开始判断发现已经有了，那么他直接释放锁，造成重复释放。

sys_open():

```c
  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);

    if (ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)) {
      int count = 10;
      while (ip->type == T_SYMLINK && count > 0) {
        if (readi(ip, 0, (uint64)path, 0, MAXPATH) < 0) {
          iunlockput(ip);
          end_op();
          return -1;
        }
        iunlockput(ip);
        if ((ip = namei(path)) == 0) {
          end_op();
          return -1;
        }
        ilock(ip);
        if (--count == 0) {
          iunlockput(ip);
          end_op();
          return -1;
        }
      }
    }

    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }
```

关键点还是锁。
