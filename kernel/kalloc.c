// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
#define SUPER_SIZE (2 * 1024 * 1024)   // 2MB
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct super_block {
  struct super_block* next;
};
static struct super_block* super_freelist = 0;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// kinit_super 在 kinit 中被调用，返回剩余内存的起始地址（供 freerange 使用）
uint64 kinit_super(void) {
  // 将内核数据结束地址 end 向上对齐到 2MB 边界
  uint64 base = (uint64)end;
  base = (base + SUPER_SIZE - 1) & ~(SUPER_SIZE - 1);

  // 预留的超级页数量（可根据需要调整，建议 8 或 16）
  int num_super_pages = 24;
  uint64 top = base + num_super_pages * SUPER_SIZE;

  // 初始化 super_freelist，将这些块串成链表
  super_freelist = 0;
  for (uint64 addr = base; addr < top; addr += SUPER_SIZE) {
    struct super_block* b = (struct super_block*)addr;
    b->next = super_freelist;
    super_freelist = b;
  }

  // 返回剩余空闲内存的起始地址（从 top 开始）
  return top;
}

void kinit() {
  initlock(&kmem.lock, "kmem");
  uint64 super_end = kinit_super();    // 预留超级页，返回剩余内存起点
  freerange((void*)super_end, (void*)PHYSTOP);   // 使用 super_end
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void* superalloc(void) {
  if (!super_freelist) return 0;
  struct super_block* b = super_freelist;
  super_freelist = b->next;
  // 清零（可选）
  memset((void*)b, 0, SUPER_SIZE);
  return (void*)b;  // 返回的地址即物理地址（因 xv6 中 PA=VA）
}

void superfree(void* pa) {
  if (!pa) return;
  struct super_block* b = (struct super_block*)pa;
  b->next = super_freelist;
  super_freelist = b;
}
