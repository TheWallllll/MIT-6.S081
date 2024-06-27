# Speed up system calls (easy)
Some operating systems (e.g., Linux) speed up certain system calls by sharing data in a read-only region between userspace and the kernel. This eliminates the need for kernel crossings when performing these system calls. To help you learn how to insert mappings into a page table, your first task is to implement this optimization for the getpid() system call in xv6.
When each process is created, map one read-only page at USYSCALL (a VA defined in memlayout.h). At the start of this page, store a struct usyscall (also defined in memlayout.h), and initialize it to store the PID of the current process. For this lab, ugetpid() has been provided on the userspace side and will automatically use the USYSCALL mapping. You will receive full credit for this part of the lab if the ugetpid test case passes when running pgtbltest.
**Some hints:**

- You can perform the mapping in proc_pagetable() in kernel/proc.c.
- Choose permission bits that allow userspace to only read the page.
- You may find that mappages() is a useful utility.
- Don't forget to allocate and initialize the page in allocproc().
- Make sure to free the page in freeproc().

**思路**
进程创建时映射一个只读的虚拟页，在memlayout.h中定义，在这个页的开头存储一个usyscall structural，初始化这个结构体存储这个进程的pid。
- hints1，在kernel/proc.c中的proc_pagtable()执行映射，查看该函数，该函数对trampoline和trapframe进行映射，应该是要求我们参考这两个映射，对USYSCALL进行映射。
- hints2，注意对这个内存页进行权限的设置。
- hints3，注意使用mappages函数
- hints4，不要忘记在allocproc()分配和初始化
- 在freeproc()释放

按照hints走即可。

# Print a page table (easy)
To help you visualize RISC-V page tables, and perhaps to aid future debugging, your second task is to write a function that prints the contents of a page table.

Define a function called vmprint(). It should take a pagetable_t argument, and print that pagetable in the format described below. Insert if(p->pid==1) vmprint(p->pagetable) in exec.c just before the return argc, to print the first process's page table. You receive full credit for this part of the lab if you pass the pte printout test of make grade.
Now when you start xv6 it should print output like this, describing the page table of the first process at the point when it has just finished exec()ing init:
```sh
page table 0x0000000087f6e000
 ..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
 .. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
 .. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
 .. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
 .. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
 ..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
 .. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
 .. .. ..509: pte 0x0000000021fdd813 pa 0x0000000087f76000
 .. .. ..510: pte 0x0000000021fddc07 pa 0x0000000087f77000
 .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```
The first line displays the argument to vmprint. After that there is a line for each PTE, including PTEs that refer to page-table pages deeper in the tree. Each PTE line is indented by a number of " .." that indicates its depth in the tree. Each PTE line shows the PTE index in its page-table page, the pte bits, and the physical address extracted from the PTE. Don't print PTEs that are not valid. In the above example, the top-level page-table page has mappings for entries 0 and 255. The next level down for entry 0 has only index 0 mapped, and the bottom-level for that index 0 has entries 0, 1, and 2 mapped.
Your code might emit different physical addresses than those shown above. The number of entries and the virtual addresses should be the same.

**Some hints:**

- You can put vmprint() in kernel/vm.c.
- Use the macros at the end of the file kernel/riscv.h.
- The function freewalk may be inspirational.
- Define the prototype for vmprint in kernel/defs.h so that you can call it from exec.c.
- Use %p in your printf calls to print out full 64-bit hex PTEs and addresses as shown in the example.

**solution**
较简单就是一个DFS。
- hints0，在kernel/vm.c写vmprint()
- hints1，使用kernel/riscv.h中的宏定义
- hints2，参考freewalk函数
- hints3，在defs.h中声明函数
- hints4，使用%p打印地址。

# Detecting which pages have been accessed (hard)
Some garbage collectors (a form of automatic memory management) can benefit from information about which pages have been accessed (read or write). In this part of the lab, you will add a new feature to xv6 that detects and reports this information to userspace by inspecting the access bits in the RISC-V page table. The RISC-V hardware page walker marks these bits in the PTE whenever it resolves a TLB miss.

Your job is to implement pgaccess(), a system call that reports which pages have been accessed. The system call takes three arguments. First, it takes the starting virtual address of the first user page to check. Second, it takes the number of pages to check. Finally, it takes a user address to a buffer to store the results into a bitmask (a datastructure that uses one bit per page and where the first page corresponds to the least significant bit). You will receive full credit for this part of the lab if the pgaccess test case passes when running pgtbltest.
**Some hints:**

- Start by implementing sys_pgaccess() in kernel/sysproc.c.
- You'll need to parse arguments using argaddr() and argint().
- For the output bitmask, it's easier to store a temporary buffer in the kernel and copy it to the user (via copyout()) after filling it with the right bits.
- It's okay to set an upper limit on the number of pages that can be scanned.
- walk() in kernel/vm.c is very useful for finding the right PTEs.
- You'll need to define PTE_A, the access bit, in kernel/riscv.h. Consult the RISC-V manual to determine its value.
- Be sure to clear PTE_A after checking if it is set. Otherwise, it won't be possible to determine if the page was accessed since the last time pgaccess() was called (i.e., the bit will be set forever).
- vmprint() may come in handy to debug page tables.

**solution**
PTE有一个flag，PTE_A,如果该页表被访问了，那么硬件会将他置为1，我们需要做的是实现一个函数，pgaccess(),一个系统调用报告哪个页被访问量，也就是标志是1。接受三个参数，需要检查的用户页的开始虚拟地址，检查的页数，存储的bitmask。
- 在kernel/sysproc.c实现函数
- 使用argaddr() and argint()接收用户参数
- 定义一个临时变量，使用copyout将结果从用kernel2user
- 设置一个最大扫描页数限制，这个可以参考测试程序，发现32即可。
- 使用walk()获取PTEs
- 在kernel/riscv.h定义PTE_A
- 检查完后将PTE_A复位