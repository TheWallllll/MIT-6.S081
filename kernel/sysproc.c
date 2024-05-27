#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 va;        //first argument: the starting virtual address of the first user page
  int num;          //second argument: the number of pages that need to check
  uint64 uaddr;     //third argument: a user address to a buffer to store the results into a bitmask

  //get the arguments
  if (argaddr(0, &va) < 0)
    return -1;
  if (argint(1, &num) < 0)
    return -1;
  if (argaddr(2, &uaddr) < 0)
    return -1;

  uint32 bitmask = 0; //32bits mask, 32 is from test(user/pgtbltest)
  uint64 mask = 1;
  pte_t* pte;
  struct proc* p = myproc();
  if (num > 32 || num < 0)
    return -1;

  //遍历num个page
  for (int i = 0;i < num;++i, va += PGSIZE) {
    if (va > MAXVA)
      return -1;
    if ((pte = walk(p->pagetable, va, 0)) == 0)
      return -1;
    //check pte_a
    if ((*pte) & PTE_A) {
      bitmask |= (mask << i);
      *pte &= ~PTE_A;
    }
  }

  //copy bitmask to user space
  if (copyout(p->pagetable, uaddr, (char*)&bitmask, sizeof(bitmask)) < 0)
    return -1;

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
