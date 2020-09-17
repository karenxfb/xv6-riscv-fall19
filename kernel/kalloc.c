// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

/*Lab7 modification*/
struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

int getcpuid()
{
  push_off();
  int cpu = cpuid();
  pop_off();
  return cpu;
}

void
kinit()
{
  int i;
  
  printf("kinit(): cpuid(%d)\n", getcpuid());
  for (i = 0; i < NCPU; i++)
  {
  	initlock(&kmems[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}
/*Lab7 modification*/


void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

/*Lab7 modification*/
void
kfree(void *pa)
{
  struct run *r;
  int cpuid = getcpuid();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  
  //printf("kfree(): cpuid(%d)\n", cpuid);
  acquire(&kmems[cpuid].lock);
  r->next = kmems[cpuid].freelist;
  kmems[cpuid].freelist = r;
  release(&kmems[cpuid].lock);
}

void *steal()
{
  struct run * rs = 0;
  int i;
  //printf("steal(): cpuid(%d)\n", getcpuid());
  
  for(i = 0; i < NCPU; i++){
    acquire(&kmems[i].lock);
    if(kmems[i].freelist != 0)
	{
      rs = kmems[i].freelist;
      kmems[i].freelist = rs->next;
      release(&kmems[i].lock);
      break;
    }
    release(&kmems[i].lock);
  }

  return (void *)rs;
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpuid = getcpuid();

  acquire(&kmems[cpuid].lock);
  r = kmems[cpuid].freelist;
  if(r)
    kmems[cpuid].freelist = r->next;
  release(&kmems[cpuid].lock);

  if (!r)
  	r = steal();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
