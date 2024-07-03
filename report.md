总体是实现lazy allocation。
三个实验之间是递补关系，有些实现基本就是混着了，反正只要最后全部实现即可。

# Eliminate allocation from sbrk() (easy)

Your first task is to delete page allocation from the sbrk(n) system call implementation, which is the function sys_sbrk() in sysproc.c. The sbrk(n) system call grows the process's memory size by n bytes, and then returns the start of the newly allocated region (i.e., the old size). Your new sbrk(n) should just increment the process's size (myproc()->sz) by n and return the old size. It should not allocate memory -- so you should delete the call to growproc() (but you still need to increase the process's size!).
删除sys_sbrk()中的页分配，只修改myproc()->sz。

**solution**
注释掉growproc(), 添加`myproc()->sz += n;`即可。

# Lazy allocation (moderate)

Modify the code in trap.c to respond to a page fault from user space by mapping a newly-allocated page of physical memory at the faulting address, and then returning back to user space to let the process continue executing. You should add your code just before the printf call that produced the "usertrap(): ..." message. Modify whatever other xv6 kernel code you need to in order to get echo hi to work.

**Here are some hints:**
1. You can check whether a fault is a page fault by seeing if r_scause() is 13 or 15 in usertrap().
2. r_stval() returns the RISC-V stval register, which contains the virtual address that caused the page fault.
3. Steal code from uvmalloc() in vm.c, which is what sbrk() calls (via growproc()). You'll need to call kalloc() and mappages().
4. Use PGROUNDDOWN(va) to round the faulting virtual address down to a page boundary.
5. uvmunmap() will panic; modify it to not panic if some pages aren't mapped.
6. If the kernel crashes, look up sepc in kernel/kernel.asm
7. Use your vmprint function from pgtbl lab to print the content of a page table.
8. If you see the error "incomplete type proc", include "spinlock.h" then "proc.h".

If all goes well, your lazy allocation code should result in echo hi working. You should get at least one page fault (and thus lazy allocation), and perhaps two.

**solution**
user 发生page fault，13和15分别是load和store发生的page fault，需要在usertrap处理。在这里实现内存分配。如果分配失败，xv6简单粗暴的杀掉进程。
kernel/trap.c usertrap()添加如下代码
```c
else if (r_scause() == 13 || r_scause() == 15) {//hint1
    uint64 va = r_stval();//hint2
    printf("Page fault %p\n", va);
    //hint3 begin
    uint64 ka = (uint64)kalloc();
    if (ka == 0) {
      p->killed = 1;
    } else {
      memset((void*)ka, 0, PGSIZE);
      va = PGROUNDDOWN(va);//hint4
      if (mappages(p->pagetable, va, PGSIZE, ka, PTE_R | PTE_U | PTE_W) != 0) {
        kfree((void*)ka);
        p->killed = 1;
      }
    }
    //hint3 end
  }
```
modify kernel/vm.c uvmunmap()
```c
if((*pte & PTE_V) == 0)
      continue;
```

# Lazytests and Usertests (moderate)

We've supplied you with lazytests, an xv6 user program that tests some specific situations that may stress your lazy memory allocator. Modify your kernel code so that all of both lazytests and usertests pass.

1. Handle negative sbrk() arguments.
2. Kill a process if it page-faults on a virtual memory address higher than any allocated with sbrk().
3. Handle the parent-to-child memory copy in fork() correctly.
4. Handle the case in which a process passes a valid address from sbrk() to a system call such as read or write, but the memory for that address has not yet been allocated.
5. Handle out-of-memory correctly: if kalloc() fails in the page fault handler, kill the current process.
6. Handle faults on the invalid page below the user stack.

```c
//hint1
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;

  struct proc* p = myproc();
  addr = p->sz;
  //if(growproc(n) < 0)
  // return -1;
  if (n < 0) {
    //n is negative
    p->sz = uvmdealloc(p->pagetable, p->sz, p->sz + n);
  } else {
    p->sz += n;
  }
  return addr;
}
```

```c
else if (r_scause() == 13 || r_scause() == 15) {
    uint64 va = r_stval();
    //printf("Page fault %p\n", va);
    //hint2 and hint6
    if (va >= p->sz || va < p->trapframe->sp) {
      //page-faults on a virtual memory address higher of equll than any allocated with sbrk() is invalid
      //also lower than stack.
      p->killed = 1;
    } else {
      uint64 ka = (uint64)kalloc();
      if (ka == 0) {//hint5
        p->killed = 1;
      } else {
        memset((void*)ka, 0, PGSIZE);
        va = PGROUNDDOWN(va);
        if (mappages(p->pagetable, va, PGSIZE, ka, PTE_R | PTE_U | PTE_W) != 0) {
          kfree((void*)ka);
          p->killed = 1;
        }
      }
    }
  }
```

for hint3, Handle the parent-to-child memory copy in fork() correctly.
在sysproc.c找到sys_fork(), 调用顺序sys_fork->fork()->uvmcopy(), 修改uvmcopy().
```c
if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
```

**for hint4, Handle the case in which a process passes a valid address from sbrk() to a system call such as read or write, but the memory for that address has not yet been allocated.(*)**
当read and write系统调用，访问了用户申请但是没有分配的内存，会发生错误，此时我们前面实现的usertrap无法完成。原因是，usertrap处理的是从用户态发出的page fault，但是read and write系统调用发生在内核空间，需要单独的处理机制。
在sys_file.c找到sys_read and sys_write,调用顺序：
sys_read()->fileread()->piperead()->copyout()
modify copyout():
方法类似usertrap的处理，但是需要注意的是，杀死进程只发生在kalloc分配失败或者mappages失败，因为这代表没有物理内存可用或者分配pte失败，这些问题xv6粗暴的杀死进程。而没有找到虚拟内存对应的物理内存，即访问用户已经申请但是没有分配的内存，那么我们需要给他lazy alloc，但是如果虚拟内存不对，那么需要返回-1，表示copyout失败，而不代表进程需要被杀死。
```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  struct proc* p = myproc();

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    //pa0 has not been allocated so walkaddr failed.
    if (pa0 == 0) {
      if (va0 >= p->sz || va0 < p->trapframe->sp) {
        return -1;
      } else {
        pa0 = (uint64)kalloc();
        if (pa0 == 0) {
          p->killed = 1;
        } else {
          memset((void*)pa0, 0, PGSIZE);
          va0 = PGROUNDDOWN(va0);
          if (mappages(p->pagetable, va0, PGSIZE, pa0, PTE_R | PTE_U | PTE_W) != 0) {
            kfree((void*)pa0);
            p->killed = 1;
          }
        }
      }
    }
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

write 同理。