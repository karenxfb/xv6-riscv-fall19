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

/*Lab8 modification*/
/*主要思路就是把队列改成多个，哈希获取，避免多个进程操作同一个链导致过多的锁操作*/
#define NBUCKETS 13

struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

void
binit(void)
{
  struct buf *b;
  int i;
	
  for (i = 0; i < NBUCKETS; i++)
  {
  	initlock(&bcache.lock[i], "bcache.bucket");
	b = &bcache.hashbucket[i];
	b->prev = b;
	b->next = b;
  }

  // Create linked list of buffers
  //bcache.head.prev = &bcache.head;
  //bcache.head.next = &bcache.head;
  //初始化时，把buffer都放在0号bucket
  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    b->next = bcache.hashbucket[0].next;
    b->prev = &bcache.hashbucket[0];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[0].next->prev = b;
    bcache.hashbucket[0].next = b;
  }
}

int bhash(int no)
{
  return no % NBUCKETS;
}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int hashno = bhash(blockno);
  int nhashno;
  
  acquire(&bcache.lock[hashno]);

  // Is the block already cached?
  for(b = bcache.hashbucket[hashno].next; b != &bcache.hashbucket[hashno]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hashno]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  
/*  // Not cached; recycle an unused buffer.
  for(b = bcache.hashbucket[hashno].prev; b != &bcache.hashbucket[hashno]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }*/

  /*当前bucket找不到buffer,就去其他bucket找，初始化时全放入0号bucket了，所以一开始很多这种情况*/
  /*如果找到对应的*/
  nhashno = bhash(hashno + 1);
  while (nhashno != hashno)
  {
  	  acquire(&bcache.lock[nhashno]);
	  for(b = bcache.hashbucket[nhashno].prev; b != &bcache.hashbucket[nhashno]; b = b->prev)
	  {
      	if(b->refcnt == 0) 
		{
        	b->dev = dev;
        	b->blockno = blockno;
        	b->valid = 0;
        	b->refcnt = 1;
        	// 从原来bucket的链表中断开
        	b->next->prev=b->prev;
        	b->prev->next=b->next;
        	release(&bcache.lock[nhashno]);
        	// 插入到blockno对应的bucket中去
        	b->next=bcache.hashbucket[hashno].next;
       	 	b->prev=&bcache.hashbucket[hashno];
        	bcache.hashbucket[hashno].next->prev=b;
        	bcache.hashbucket[hashno].next=b;
        	release(&bcache.lock[hashno]);
        	acquiresleep(&b->lock);
        	return b;
      	}
      }
	  release(&bcache.lock[nhashno]);
	  nhashno = bhash(nhashno + 1);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
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
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  int hashno;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  hashno = bhash(b->blockno);
  acquire(&bcache.lock[hashno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.  /*插入到bucket头部, 保证最近常使用的在前面*/
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[hashno].next;
    b->prev = &bcache.hashbucket[hashno];   
    bcache.hashbucket[hashno].next->prev = b;
    bcache.hashbucket[hashno].next = b;
  }
  
  release(&bcache.lock[hashno]);
}

void
bpin(struct buf *b) {
  int hashno = bhash(b->blockno);
  
  acquire(&bcache.lock[hashno]);
  b->refcnt++;
  release(&bcache.lock[hashno]);
}

void
bunpin(struct buf *b) {
  int hashno = bhash(b->blockno);

  acquire(&bcache.lock[hashno]);
  b->refcnt--;
  release(&bcache.lock[hashno]);
}


