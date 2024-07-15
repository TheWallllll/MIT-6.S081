// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

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
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
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

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
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

void
bpin(struct buf* b) {
  uint key = BUFMAP_HASH(b->blockno);
  acquire(&bcache.locks[key]);
  b->refcnt++;
  release(&bcache.locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->blockno);
  acquire(&bcache.locks[key]);
  b->refcnt--;
  release(&bcache.locks[key]);
}


