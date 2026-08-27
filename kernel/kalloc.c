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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// reference counts for copy-on-write pages. Each physical page is
// counted by the number of user page tables that reference it.
int refcount[(PHYSTOP - KERNBASE) / PGSIZE];

static inline int
refidx(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// increment the reference count of a physical page (fork shares it).
void
krefinc(uint64 pa)
{
  acquire(&kmem.lock);
  refcount[refidx(pa)]++;
  release(&kmem.lock);
}

// return the reference count of a physical page.
int
krefget(uint64 pa)
{
  int n;
  acquire(&kmem.lock);
  n = refcount[refidx(pa)];
  release(&kmem.lock);
  return n;
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int idx;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // decrement the reference count; only actually free when it hits zero.
  idx = refidx((uint64)pa);

  acquire(&kmem.lock);
  if(refcount[idx] > 1){
    refcount[idx]--;
    release(&kmem.lock);
    return;
  }
  refcount[idx] = 0;
  release(&kmem.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    refcount[refidx((uint64)r)] = 1;
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}
