# Memory allocator(moderate)

kalloc中的锁争用是因为kalloc中只有一个空闲链表，所以要消除，可以为每个cpu维护一个空闲链表，每个空闲链表有自己的锁。如果cpu的空闲链表为空，那么从其他cpu处窃取。

**hints**

* You can use the constant NCPU from kernel/param.h
* Let freerange give all free memory to the CPU running freerange.
* The function cpuid returns the current core number, but it's only safe to call it and use its result when interrupts are turned off. You should use push\_off() and pop\_off() to turn interrupts off and on.
* Have a look at the snprintf function in kernel/sprintf.c for string formatting ideas. It is OK to just name all locks "kmem" though.

**analysis**

kalloc 原本的实现中，使用 freelist 链表，将空闲物理页**本身**直接用作链表项（这样可以不使用额外空间）连接成一个链表，在分配的时候，将物理页从链表中移除，回收时将物理页放回链表中。

无论是分配还是回收都需要修改freelist链表，为了保证不变量，必须加锁，但是导致多线程并发效率很低，从kalloctest中可以看到kmem锁频繁存在竞争。

这里体现了先profile再优化的思路，如果一个大锁不会明显影响性能，那么大锁就够了，换句话说，如果降低锁的颗粒度不能明显提升性能就不要优化锁，优化锁是一件非常麻烦的事，而且很容易产生死锁、不变量被破坏等灾难。【过早优化是万恶之源】

锁竞争优化思路一般有：

- 值再必须共享时共享。即拆分共享资源，每个cpu都有。
- 必须共享时减少在关键区停留的事件。即大锁化小锁，降低锁的粒度。

此处可以使用方法一。每个cpu分配一个freelist，每个freelist有自己的锁。最大的难点在于如何从别的cpu处偷页。

**solution1：stealing 128 pages each time**

注意在获取cpuid时关闭中断。

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  char buf[10];
  for (int i = 0;i < NCPU;++i) {
    snprintf(buf, 10, "kmem_CPU%d", i);
    initlock(&kmem[i].lock, buf);
  }
  
  freerange(end, (void*)PHYSTOP);
}
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpunum = cpuid();

  acquire(&kmem[cpunum].lock);
  r->next = kmem[cpunum].freelist;
  kmem[cpunum].freelist = r;
  release(&kmem[cpunum].lock);

  pop_off();
}
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpunum = cpuid();

  acquire(&kmem[cpunum].lock);
  if(!kmem[cpunum].freelist) {//steal 128 page from other CPU's freelist
    int stealnum = 128;
    for (int i = 0;i < NCPU;++i) {
      if (i == cpunum)
        continue;
      acquire(&kmem[i].lock);
      struct run* tmp = kmem[i].freelist;
      while (tmp && stealnum) {
        kmem[i].freelist = tmp->next;
        tmp->next = kmem[cpunum].freelist;
        kmem[cpunum].freelist = tmp;
        tmp = kmem[i].freelist;
        stealnum--;
      }
      release(&kmem[i].lock);
      if (!stealnum)
        break;
    }
  }

  r = kmem[cpunum].freelist;
  if (r)
    kmem[cpunum].freelist = r->next;
  release(&kmem[cpunum].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

此处存在问题，虽然能够通过测试，但是会有死锁风险，如果cpu[0]去偷cpu[1]，此时他hold lock[0]，尝试获取lock[1]。于此同时，cpu[1]偷cpu[0]，发生死锁。

**solution2: stealing one page each time**

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpunum = cpuid();

  acquire(&kmem[cpunum].lock);
  r = kmem[cpunum].freelist;
  if (r)
    kmem[cpunum].freelist = r->next;
  release(&kmem[cpunum].lock);
  if(!r) {//steal one page from other CPU's freelist
    for (int i = 0;i < NCPU;++i) {
      if (i == cpunum)
        continue;
      acquire(&kmem[i].lock);
      if (kmem[i].freelist) {
        r = kmem[i].freelist;
        kmem[i].freelist = kmem[i].freelist->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

这种方法并没有把偷来的页表放回本cpu的freelist，而是直接作为结果返回，等到free的时候被放回freelist，因此可以提前释放，因为并没有修改自己的数据结构，所以不会死锁，但是性能可能不太好，如果一次有大量的页表分配。

# Buffer cache(hard)(是真的难，最难的一集 :sob:)

文件系统buffer cache的bcache.lock锁争用，修改块缓存，主要是bget和brelse。必须保护每个块只有一个缓存的不变量。

不同于kalloc，buffer cache真正的在进程之间共享，因此不可以破坏，所以只能大锁化小锁。可能的想法是使用哈希桶，每个哈希桶一个锁。

**Here are some hints:**

* Read the description of the block cache in the xv6 book (Section 8.1-8.3).
* It is OK to use a fixed number of buckets and not resize the hash table dynamically. Use a prime number of buckets (e.g., 13) to reduce the likelihood of hashing conflicts.
* Searching in the hash table for a buffer and allocating an entry for that buffer when the buffer is not found must be atomic.
* Remove the list of all buffers (bcache.head etc.) and instead time-stamp buffers using the time of their last use (i.e., using ticks in kernel/trap.c). With this change brelse doesn't need to acquire the bcache lock, and bget can select the least-recently used block based on the time-stamps.
* It is OK to serialize eviction in bget (i.e., the part of bget that selects a buffer to re-use when a lookup misses in the cache).
* Your solution might need to hold two locks in some cases; for example, during eviction you may need to hold the bcache lock and a lock per bucket. Make sure you avoid deadlock.
* When replacing a block, you might move a struct buf from one bucket to another bucket, because the new block hashes to a different bucket. You might have a tricky case: the new block might hash to the same bucket as the old block. Make sure you avoid deadlock in that case.
* Some debugging tips: implement bucket locks but leave the global bcache.lock acquire/release at the beginning/end of bget to serialize the code. Once you are sure it is correct without race conditions, remove the global locks and deal with concurrency issues. You can also run make CPUS=1 qemu to test with one core

**analysis**

原版 xv6 的设计中，使用双向链表存储所有的区块缓存，每次尝试获取一个区块 blockno 的时候，会遍历链表，如果目标区块已经存在缓存中则直接返回，如果不存在则选取一个最近最久未使用的，且引用计数为 0 的 buf 块作为其区块缓存，并返回。

新的改进方案，可以**建立一个从 blockno 到 buf 的哈希表，并为每个桶单独加锁**。这样，仅有在两个进程同时访问的区块同时哈希到同一个桶的时候，才会发生锁竞争。当桶中的空闲 buf 不足的时候，从其他的桶中获取 buf。

思路不难，但是死锁问题和保护不变量存在很大的困难 😭 😭 😭

首先按照提示，修改数据结构，

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint time;   //used to keep track of the least-recently-used buf
  struct buf *next;
  uchar data[BSIZE];
};
```

```c
#define NBUCKET 13
#define BUFMAP_HASH(blockno) (blockno % NBUCKET)

struct {
  struct spinlock locks[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf bufmap[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf* b;

  for (int i = 0; i < NBUCKET; ++i) {
    initlock(&bcache.locks[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->time = 0;
    b->refcnt = 0;
    initsleeplock(&b->lock, "buffer");
    //头插法
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
}
```

下面的bget()需要仔细处理。

首先很容易想到的思路是，先获取key桶的锁，然后查找，找到就解锁返回，找不到，就遍历所有桶找到一个可用的，放在自己的桶里，然后返回，很容易写入如下伪代码(没有很规范的写)：

```c
bget(dev, blockno) {
  key := hash(blockno);

  acquire(locks[key]); // 获取 key 桶的锁
  
  // 查找 blockno 的缓存是否存在，若是直接返回，若否继续执行
  if(b := look_for_blockno_in(bufmap[key])) {
    b->refcnt++
    release(bufmap_locks[key]);
    acquiresleep();
    return b;
  }

  // 查找可驱逐缓存 b
  
  least_recently := NULL;
  
  for i := [0, NBUFMAP_BUCKET) { // 遍历所有的桶
    acquire(bufmap_locks[i]);    // 获取第 i 桶的锁

    b := look_for_least_recently_used_with_no_ref(bufmap[key]);
    // 如果找到未使用时间更长的空闲块
    if(b.last_use < least_recently.last_use) {  
      least_recently := b;
    }

    release(bufmap_locks[i]);   // 查找结束后，释放第 i 桶的锁
  }

  b := least_recently;

  // 驱逐 b 原本存储的缓存（将其从原来的桶删除）
  evict(b);

  // 将 b 加入到新的桶
  append(bucket[key], b);

  release(bufmap_locks[key]); // 释放 key 桶的锁

  // 设置 b 的各个属性
  setup(b);
  return b;
}
```

存在两个问题：

1. 可驱逐的buf在释放对应的桶的锁之后不能保证仍可驱逐。
   如果找到驱逐的block，释放他所在的桶的锁之后，从原来所在的桶中删除以前，其他的cpu完全可能调用bget使用这个块导致他的引用计数不为0，此时他不能被释放。解决办法是保证找到和删除操作时原子的。在找到该块之后并不释放锁，而是直到删除它或者找到另一个更合适的块之后再释放。
2. 死锁

```
假设块号 b1 的哈希值是 2，块号 b2 的哈希值是 5
并且两个块在运行前都没有被缓存
----------------------------------------
CPU1                  CPU2
----------------------------------------
bget(dev, b1)         bget(dev,b2)
    |                     |
    V                     V
获取桶 2 的锁           获取桶 5 的锁
    |                     |
    V                     V
缓存不存在，遍历所有桶    缓存不存在，遍历所有桶
    |                     |
    V                     V
  ......                遍历到桶 2
    |                尝试获取桶 2 的锁
    |                     |
    V                     V
  遍历到桶 5          桶 2 的锁由 CPU1 持有，等待释放
尝试获取桶 5 的锁
    |
    V
桶 5 的锁由 CPU2 持有，等待释放

!此时 CPU1 等待 CPU2，而 CPU2 在等待 CPU1，陷入死锁!
```

这里的死锁处理较为复杂。

死锁的四个条件：

1. 互斥（一个资源在任何时候只能属于一个线程）
2. 请求保持。（线程持有一个锁去拿另一个锁）
3. 不剥夺。（外力不强制剥夺一个线程已经持有的资源）
4. 环路等待。（请求资源的顺序存在环路）

破坏互斥和不剥夺条件显然不可能。考虑从请求保持和环路等待入手

**solution1**

破坏环路等待。

出现死锁的原因是进程之间的请求出现了环路，那么我们可以让两个进程之间的请求没有交叉，这样就不存在环路。因此我们可以让每次查找可驱逐的桶只发生在源桶的左侧，我们现在定义了13个桶，一次只遍历一半6个的桶，例如12号只会遍历12、11、10、9、8、7、6（带自己7个），此时6号遍历6，5，4，3，2，1，0，不可能发生环路等待，所以可行。

经过测试发现带自己6个性能更好，因此使用6个，不过可能的缺点在于，如果在前边的6个中没有但是后边的有很多，这种方法会找不到桶而崩溃，**（所以使用素数个桶、合适的哈希函数保证分布的均匀很重要）**

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = BUFMAP_HASH(blockno);
  acquire(&bcache.locks[key]);

  // Is the block already cached?
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  uint bucketnum = NBUCKET / 2;
  struct buf* before_target = 0;
  uint holdlock = -1;
  for (int i = 0; i < bucketnum; ++i) {
    int index = key - i;
    if (index < 0)
      index += NBUCKET;
    if (index != key)
      acquire(&bcache.locks[index]);

    int newfound = 0;
    for (b = &bcache.bufmap[index]; b->next; b = b->next) {
      if (b->next->refcnt == 0 && (before_target == 0 || b->next->time < before_target->next->time)) {
        before_target = b;
        newfound = 1;
      }
    }

    if (newfound) {
      if (holdlock != -1 && holdlock!=key)
        release(&bcache.locks[holdlock]);
      holdlock = index;
    }
    else if (index != key) {
      release(&bcache.locks[index]);
    }
  }

  if (before_target == 0)
    panic("bget: no buffers");

  b = before_target->next;

  if (holdlock != key) {
    before_target->next = b->next;
    release(&bcache.locks[holdlock]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }

  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  release(&bcache.locks[key]);
  acquiresleep(&b->lock);

  return b;
}
```

**solution2**

破坏请求保持条件，在cache miss之后查找可驱逐桶时，先释放key桶的锁。此时会存在问题，如果一个线程释放了key桶的锁，而另外一个线程同样是使用的这个桶，此时它可以获取该桶的锁，发现cache miss，然后也去搜索，最后一个块有了两个buffer cache，破坏了每个块只有一个缓存的不变量。因此我们在释放掉key桶的锁之后需要先获取一个驱逐锁保证整体数据结构的不变。注意顺序不能反，必须先释放后获取，不然又会产生死锁。

获取驱逐锁之后需要再次判断是否有对应的缓存，因为另外的进程使用同样的桶的可能刚刚加过来一个已经有了，此时你直接返回就行了。

这种方法的缺点是效率会低一些，因为驱逐锁的存在会使并发性降低。但是由于cachemiss本身比较少，所以可能还好。反正测试挺快的，和solution1感觉没区别。

> ps. 这样的设计，有一个名词称为「乐观锁（optimistic locking）」，即在冲突发生概率很小的关键区内，不使用独占的互斥锁，而是在提交操作前，检查一下操作的数据是否被其他线程修改（在这里，检测的是 blockno 的缓存是否已被加入），如果是，则代表冲突发生，需要特殊处理（在这里的特殊处理即为直接返回已加入的 buf）。这样的设计，相比较「悲观锁（pessimistic locking）」而言，可以在冲突概率较低的场景下（例如 bget），降低锁开销以及不必要的线性化，提升并行性（例如在 bget 中允许「缓存是否存在」的判断并行化）。有时候还能用于避免死锁。

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = BUFMAP_HASH(blockno);
  acquire(&bcache.locks[key]);

  // Is the block already cached?
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //Not cached
  release(&bcache.locks[key]);
  //to get a suitable block to reuse, we need to search for one in all buckets,
  //which, however, may generate deadlock. For example, bucket[key] hold its own lock,
  //and it acquire lock[B], bucket[B] hold its own lock and acquire lock[key].
  //so we release lock[key]. But we need a new lock to protect the invariant. 
  //Because if one thread release lock[key], and search for a block in other buckets,
  //other thread can acquire lock[key] and this thread find no cache, so it will search too.
  //Finally, the block who's number is blockno will have two identical cache.
  acquire(&bcache.eviction_lock);
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if (b->dev == dev && b->blockno == blockno) {
      acquire(&bcache.locks[key]);
      b->refcnt++;
      release(&bcache.locks[key]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  struct buf* before_target = 0;
  uint holdlock = -1;
  for (int i = 0; i < NBUCKET; ++i) {

    acquire(&bcache.locks[i]);

    int newfound = 0;
    for (b = &bcache.bufmap[i]; b->next; b = b->next) {
      if (b->next->refcnt == 0 && (before_target == 0 || b->next->time < before_target->next->time)) {
        before_target = b;
        newfound = 1;
      }
    }

    if (newfound) {
      if (holdlock != -1)
        release(&bcache.locks[holdlock]);
      holdlock = i;
    }
    else {
      release(&bcache.locks[i]);
    }
  }

  if (before_target == 0)
    panic("bget: no buffers");

  b = before_target->next;

  if (holdlock != key) {
    before_target->next = b->next;
    release(&bcache.locks[holdlock]);
    acquire(&bcache.locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }

  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  release(&bcache.locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);

  return b;
}
```

对relse的修改很简单了就，不需要修改他在链表中的位置，因为我们使用一个单独的filed判断least-recently。

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->blockno);
  
  acquire(&bcache.locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->time = ticks;
  }
  release(&bcache.locks[key]);
}
```

另外需要bache.lock的都换成对应的桶锁即可。
