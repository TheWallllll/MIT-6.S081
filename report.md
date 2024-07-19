# mmap(hard)

mmap`和`munmap`系统调用允许UNIX程序对其地址空间进行详细控制。它们可用于在进程之间共享内存，将文件映射到进程地址空间，并作为用户级页面错误方案的一部分。

`man 2 mmap`显示：

```c
void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void *addr, size_t length);
```

假设addr始终为0，`mmap`返回该地址，如果失败则返回`0xffffffffffffffff`。`length`是要映射的字节数；它可能与文件的长度不同。`prot`指示内存是否应映射为可读、可写，以及/或者可执行的；您可以认为`prot`是`PROT_READ`或`PROT_WRITE`或两者兼有。`flags`要么是`MAP_SHARED`（映射内存的修改应写回文件），要么是`MAP_PRIVATE`（映射内存的修改不应写回文件）。您不必在`flags`中实现任何其他位。`fd`是要映射的文件的打开文件描述符。可以假定`offset`为零（它是要映射的文件的起点）。

`munmap(addr, length)`应删除指定地址范围内的`mmap`映射。如果进程修改了内存并将其映射为`MAP_SHARED`，则应首先将修改写入文件。`munmap`调用可能只覆盖`mmap`区域的一部分，但您可以认为它取消映射的位置要么在区域起始位置，要么在区域结束位置，要么就是整个区域(但不会在区域中间“打洞”)。

**Here are some hints:**

* Start by adding \_mmaptest to UPROGS, and mmap and munmap system calls, in order to get user/mmaptest.c to compile. For now, just return errors from mmap and munmap. We defined PROT\_READ etc for you in kernel/fcntl.h. Run mmaptest, which will fail at the first mmap call.
* Fill in the page table lazily, in response to page faults. That is, mmap should not allocate physical memory or read the file. Instead, do that in page fault handling code in (or called by) usertrap, as in the lazy page allocation lab. The reason to be lazy is to ensure that mmap of a large file is fast, and that mmap of a file larger than physical memory is possible.
* Keep track of what mmap has mapped for each process. Define a structure corresponding to the VMA (virtual memory area) described in Lecture 15, recording the address, length, permissions, file, etc. for a virtual memory range created by mmap. Since the xv6 kernel doesn't have a memory allocator in the kernel, it's OK to declare a fixed-size array of VMAs and allocate from that array as needed. A size of 16 should be sufficient.
* Implement mmap: find an unused region in the process's address space in which to map the file, and add a VMA to the process's table of mapped regions. The VMA should contain a pointer to a struct file for the file being mapped; mmap should increase the file's reference count so that the structure doesn't disappear when the file is closed (hint: see filedup). Run mmaptest: the first mmap should succeed, but the first access to the mmap-ed memory will cause a page fault and kill mmaptest.
* Add code to cause a page-fault in a mmap-ed region to allocate a page of physical memory, read 4096 bytes of the relevant file into that page, and map it into the user address space. Read the file with readi, which takes an offset argument at which to read in the file (but you will have to lock/unlock the inode passed to readi). Don't forget to set the permissions correctly on the page. Run mmaptest; it should get to the first munmap.
* Implement munmap: find the VMA for the address range and unmap the specified pages (hint: use uvmunmap). If munmap removes all pages of a previous mmap, it should decrement the reference count of the corresponding struct file. If an unmapped page has been modified and the file is mapped MAP\_SHARED, write the page back to the file. Look at filewrite for inspiration.
* Ideally your implementation would only write back MAP\_SHARED pages that the program actually modified. The dirty bit (D) in the RISC-V PTE indicates whether a page has been written. However, mmaptest does not check that non-dirty pages are not written back; thus you can get away with writing pages back without looking at D bits.
* Modify exit to unmap the process's mapped regions as if munmap had been called. Run mmaptest; mmap\_test should pass, but probably not fork\_test.
* Modify fork to ensure that the child has the same mapped regions as the parent. Don't forget to increment the reference count for a VMA's struct file. In the page fault handler of the child, it is OK to allocate a new physical page instead of sharing a page with the parent. The latter would be cooler, but it would require more implementation work. Run mmaptest; it should pass both mmap\_test and fork\_test.

**solution**

题目说明了addr和offset都默认为0，但是下面的实现还是使用了offset。如果不加offset会更简单。

(1.) 首先添加系统调用。不再赘述

(2.) 根据提示3，定义vma结构体，并添加到 struct proc。kernel/proc.h

(3.) 根据hint2 and 3、4，参考之前的lab lazy，只是告诉用户我分配了，设置了对应的数据结构，但是没有实际分配物理页，用到的时候发生缺页然后分配。虚拟地址的选取使用p->sz。注意的点是，一些权限的判断，如果文件不可写，那么映射的内存可写且要写回文件显然是不被允许的。增加文件的引用次数，使用filedup()。将系统调用的参数赋值给vma结构体即可。

(4.) 根据提示5在usertrap中实现lazy allocation。主要工作是分配物理页，读取文件内容，添加映射。

首先要对造成缺页的虚拟地址进行判断，因为我们的vma放在p->sz，所以这里va必须高于栈低于堆顶，如果低于栈`va < p->trapframe->sp`，高于堆顶`va >= p->sz`都是不可以的。

遍历进程的vma数组找到哪个vma缺页，然后分配物理页，使用readi()读文件，设置标志位最后映射。
注意readi()时需要锁并且，offset需要仔细计算。

此时基本已经可以通过mmap的测试

(5.) 根据hint6实现munmap，hint7说明不需要查看脏位。需要完成的工作是取消映射，如果整个vma都被取消那么文件的引用-1，如果需要写回使用filewrite。

根据前边的要求，munmap只可能在开头或者结尾，不可能在中间挖洞，如果这样可能需要一分为二，实现较为麻烦，但是感觉也不难。

(6.) 之前的lazy lab中lazy allocation call uvmunmp() and fork call uvmcopy() 都会在检查v标志位时panic，所以需要修改。

(7.) 根据提示8修改exit，其实就是进程退出所有的vma都要取消映射，需要写回的写回。

(8.) 根据提示9修改fork，主要是增加一个引用计数。以及vma数组的复制。