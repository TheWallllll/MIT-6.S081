1. Modify uvmcopy() to map the parent's physical pages into the child, instead of allocating new pages. Clear PTE_W in the PTEs of both child and parent.
2. Modify usertrap() to recognize page faults. When a page-fault occurs on a COW page, allocate a new page with kalloc(), copy the old page to the new page, and install the new page in the PTE with PTE_W set.
3. Ensure that each physical page is freed when the last PTE reference to it goes away -- but not before. A good way to do this is to keep, for each physical page, a "reference count" of the number of user page tables that refer to that page. Set a page's reference count to one when kalloc() allocates it. Increment a page's reference count when fork causes a child to share the page, and decrement a page's count each time any process drops the page from its page table. kfree() should only place a page back on the free list if its reference count is zero. It's OK to to keep these counts in a fixed-size array of integers. You'll have to work out a scheme for how to index the array and how to choose its size. For example, you could index the array with the page's physical address divided by 4096, and give the array a number of elements equal to highest physical address of any page placed on the free list by kinit() in kalloc.c.
4. Modify copyout() to use the same scheme as page faults when it encounters a COW page.

**Some hints:**

- The lazy page allocation lab has likely made you familiar with much of the xv6 kernel code that's relevant for copy-on-write. However, you should not base this lab on your lazy allocation solution; instead, please start with a fresh copy of xv6 as directed above.
- It may be useful to have a way to record, for each PTE, whether it is a COW mapping. You can use the RSW (reserved for software) bits in the RISC-V PTE for this.
- usertests explores scenarios that cowtest does not test, so don't forget to check that all tests pass for both.
- Some helpful macros and definitions for page table flags are at the end of kernel/riscv.h.
- If a COW page fault occurs and there's no free memory, the process should be killed.

**solution**
实现copy on write。
fork时，子进程不立即分配物理页，而是创建page table指向父进程的物理页。为了保证进程之间的隔离性，需要设置这些物理页不可写。当有进程需要修改时，由于PTE_W为0，发生page fault，调用usertrap处理，此时，分配物理页表并拷贝，flags修改为可写。这里有一点需要注意，不管是父进程还是子进程，对物理页的修改改都发生page fault。
难点在于物理页的释放问题，如果每个进程释放一次，那么毫无疑问会有重复释放问题，因此我们需要一个计数器，每有一个page table指向物理页表，对应该物理页表的计数器加一，只有当计数器为0时kfree该页表。按照lab要求，使用一个定长数组，存储这个计数器。另外的问题是，copyout也会对进程的物理页表进行写入，而他发生在内核态，因此需要参照usertrap对copyout也进行相似处理。

- 在riscv.h中定义RSW标志位
- 在kalloc.c中定义全局数组计时器，注意需要同时定义一个锁，因为这个数组是所有进程共享的，因此需要上锁。在kalloc时将对应的cnt设置为1，每次free检查，只有等于0时才释放，其余时候只是将对应计数器减一。
- 在fork时，不分配页表，修改对应标志位，PTE_W置为0，PTR_RSW置为1，同时将对应的物理页表的计时器加一，映射子进程的va到相同的pa。
- 由于usertrap和copyout需要相似功能，因此将该功能封装为一个函数，定义在vm.c中。得到pte，检查标志位，符合条件后分配物理内存，将原来的页表内容copy过去，最后修改标志位，同时释放页表。（其实在kfree内部只是将计数器-1，后续可以优化应该。）
  ```c
  int
  cow(pagetable_t pagetable, uint64 va) {
    if (va >= MAXVA)
      return -1;

    pte_t* pte = walk(pagetable, va, 0);
    if (pte == 0)
      return -1;
    if ((*pte & PTE_V) == 0 || (*pte & PTE_RSW) == 0 || (*pte & PTE_U) == 0)
      return -1;

    uint64 pa = PTE2PA(*pte);
    uint64 ka = (uint64)kalloc();
    if (ka == 0)
      return -1;
    memmove((void*)ka, (void*)pa, PGSIZE);

    uint64 flags = PTE_FLAGS(*pte);
    *pte = PA2PTE(ka) | flags | PTE_W;
    *pte &= ~PTE_RSW;
    kfree((void*)pa);

    return 0;
  }
  ```
- 在usertrap调用上述函数。在copyout中同样如果此，需要注意的是调用时机，因为有可能只是一个普通的copyout，此时没有cow。

**容易出错的点**，其实是面向测试程序编程，完善代码。

- 标志位设置问题。
- 新定义的cow函数中，对maxva的检查，这点在后边的测试程序中会有相应的测试。
- cow.c中最后释放kfree()一定要有。
- **这段代码出现顺序：**
  ```c
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
  ```
  有一种可能的情况，传入copyout的dstva过大，如果这段代码在cow之后出现，虽然可以省略最后重新修改pa0的情况，但是walk在面对超过MAXVA的地址会直接panic，而walkaddr则是return 0，因此需要先进行walkaddr。
- 上述问题中，不要忘记最后重新修改pa0。
