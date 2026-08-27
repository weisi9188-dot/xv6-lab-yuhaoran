#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

extern char trampoline[]; // trampoline.S

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// map ELF permissions to PTE permission bits.
int flags2perm(int flags)
{
    int perm = 0;
    if(flags & 0x1)
      perm = PTE_X;
    if(flags & 0x2)
      perm |= PTE_W;
    return perm;
}

//
// the implementation of the exec() system call
//
int
kexec(char* path, char** argv)
{
  char* s, * last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode* ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc* p = myproc();

  // ========== 新增：用于新 USYSCALL 页的指针 ==========
  struct usyscall* new_usyscall = 0;
  // ================================================

  begin_op();

  if ((ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);

  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  if (elf.magic != ELF_MAGIC)
    goto bad;

  pagetable = uvmcreate();
  if (pagetable == 0) goto bad;
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0) goto bad;
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe), PTE_R | PTE_W) < 0) goto bad;

  // ========== 新增：分配并映射 USYSCALL 页 ==========
  // 分配一页物理内存给 usyscall
  if ((new_usyscall = (struct usyscall*)kalloc()) == 0)
    goto bad;
  // 填入当前进程的 PID（exec 不改变 PID）
  new_usyscall->pid = p->pid;
  // 将这块物理内存映射到用户虚拟地址 USYSCALL，权限为用户态只读
  if (mappages(pagetable, USYSCALL, PGSIZE,
    (uint64)new_usyscall, PTE_R | PTE_U) < 0) {
    kfree(new_usyscall);
    new_usyscall = 0;
    goto bad;
  }
  // ================================================

  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if ((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK * PGSIZE;

  for (argc = 0; argv[argc]; argc++) {
    if (argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16;
    if (sp < stackbase)
      goto bad;
    if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  if (copyout(pagetable, sp, (char*)ustack, (argc + 1) * sizeof(uint64)) < 0)
    goto bad;

  p->trapframe->a1 = sp;

  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;
  p->trapframe->sp = sp;

  // ========== 新增：替换旧的 usyscall ==========
  // 保存旧的 usyscall 指针
  struct usyscall* old_usyscall = p->usyscall;
  p->usyscall = new_usyscall;   // 指向新分配的
  // ============================================

  proc_freepagetable(oldpagetable, oldsz);

  // ========== 新增：释放旧的 usyscall 物理页 ==========
  if (old_usyscall)
    kfree((void*)old_usyscall);
  // ==================================================

  return argc;

bad:
  if (new_usyscall)
    kfree((void*)new_usyscall);
  if (pagetable)
    proc_freepagetable(pagetable, sz);
  if (ip) {
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load an ELF program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
