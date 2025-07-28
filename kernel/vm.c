#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit(){
    //全局内核页表仍然用kvminit函数来初始化
   kernel_pagetable = cyh_kvminit_newpgtbl();
   kvmmap(kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.

//void
//kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
//{
//  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
//    panic("kvmmap");
//}

void
kvmmap(pagetable_t pgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
    if(mappages(pgtbl, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(pagetable_t pgtbl, uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(pgtbl, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

//uint64
//kvmpa(uint64 va)
//{
 // uint64 off = va % PGSIZE;
 // pte_t *pte;
 // uint64 pa;
  
 // pte = walk(va, 0);
 // if(pte == 0)
  //  panic("kvmpa");
 // if((*pte & PTE_V) == 0)
 //   panic("kvmpa");
 // pa = PTE2PA(*pte);
 // return pa+off;
//}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.

int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}


int
cyh_pgtblprint(pagetable_t pagetable, int depth){
    //一个页表里有 2^9 = 512 PTEs 
    for (int i = 0; i < 512; i++){
        pte_t pte = pagetable[i];

        if(pte & PTE_V){//如果页表项有效，按格式打印页表项 与PTE_V按位与
            printf("..");
            for(int j = 0; j < depth; ++j)
                printf(" ..");
            printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
            //%p：格式化输出一个指针（地址）
            //%d：格式化输出一个带符号十进制整数

            //如果该节点不是叶节点，递归打印子节点
            if((pte & (PTE_R | PTE_W | PTE_X)) == 0){
                //这个pte指向低一级别的页表
                uint64 child = PTE2PA(pte);
                cyh_pgtblprint((pagetable_t)child, depth + 1);
                //child只是下一个页表所在的起始物理地址，
                //而下一个页表的第一个pte是这个地址上的值
            }
        }
    }
    return 0;
}

//打印页表
int
cyh_vmprint(pagetable_t pagetable){
    printf("page table %p\n", pagetable);
    return cyh_pgtblprint(pagetable, 0);
}


void
cyh_kvm_map_pagetable(pagetable_t pgtbl){
    //将内核所需要的直接映射添加到页表pgtbl中
    //PTE_R | PTE_W 就表示可以读写，不能执行（PTE_X）
    //uart registers
    kvmmap(pgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    //virtio mmio disk interface
    kvmmap(pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    //CLINT
    //kvmmap(pgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

    //PLIC
    kvmmap(pgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    //map kernel text executable and read-only.
    kvmmap(pgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

    //map kernel data and the physical RAM we'll make use of.
    kvmmap(pgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

    //map the trampoline for trap entry/exit to
    //the highest virtual address in the kernel.
    kvmmap(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

void
cyh_kvm_free_kernelpgtbl(pagetable_t pagetable){
    for(int i = 0; i < 512; i++){
        pte_t pte = pagetable[i];
        uint64 child = PTE2PA(pte);
        if((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0){
            cyh_kvm_free_kernelpgtbl((pagetable_t)child);
            pagetable[i] = 0;
        }
    }
    kfree((void*)pagetable); //释放当前级别页表所占的空间
}

pagetable_t
cyh_kvminit_newpgtbl()
{
    //kalloc 函数从内核的空闲物理内存中分配出一个完整的页（4096字节）
    pagetable_t pgtbl = (pagetable_t) kalloc();
    //memset函数将这个页表中所有页表项的valid位清零，创建了一个完全为空、没有任何映射的页表
    memset(pgtbl, 0, PGSIZE); 
    //填充所有内核映射到页表中
    cyh_kvm_map_pagetable(pgtbl);

    return pgtbl;
}
//将src页表的一部分页映射关系拷贝到dst页表中。只拷贝页表项，不拷贝实际的物理内存
int
cyh_kvmcopymappings(pagetable_t src, pagetable_t dst, uint64 start, uint64 sz)
{
    pte_t* pte;
    uint64 pa,i;
    uint flags;

    //PGROUNDUP:将地址向上取整到页边界 4 KB， 防止重新映射已经映射的页，特别是在执行growproc操作时
    for(i = PGROUNDUP(start); i < start + sz; i += PGSIZE){
        // walk()函数的作用是：在给定的页表'src'中，查找虚拟地址'i'对应的最底层的PTE。
        // 也就是PPN，最终物理地址的高44位
        // 第三个参数 '0' 表示如果中间级别的页表不存在，不要创建新的。
        // 在这个函数中，我们期望源页表'src'中的映射是已经存在的，所以如果walk返回0（表示找不到PTE），
        // 那就是一个严重的内核错误，系统应该立即停止（panic）。
        if((pte = walk(src, i, 0)) == 0)
            panic("kvmcopymappings: pte should exist");
        // 检查找到的PTE的有效位（Valid bit）。如果这个位是0，说明这个映射是无效的。
        // 同样，对于一个要被复制的内核映射，我们期望它总是有效的。如果无效，则触发panic。
        if((*pte & PTE_V) == 0)
            panic("kvmcopymappings: page not present");
        
        pa = PTE2PA(*pte);

        //'&~PTE_U'表示将该页的权限设置为非用户页
        //必须设置该权限，因为RISC-V中内核无法直接访问用户页
        //PTE_U是“用户可访问”标志位。
        // 整个表达式的作用是：复制源PTE的所有标志位，但强制性地将“用户可访问”位清零。
        // 这是为了确保新创建的内核映射只能被内核访问，防止用户程序意外或恶意地访问到内核空间。
        flags = PTE_FLAGS(*pte) & ~PTE_U;
        // mappages()函数在目标页表'dst'中，为虚拟地址'i'创建一个新的映射。
        // 它将虚拟地址'i'映射到我们刚刚从源PTE中得到的物理地址'pa'，并赋予指定的权限'flags'。
        // 如果mappages失败（例如，因为创建中间页表时内存不足），它会返回一个非零值。
        // 若 dst 里缺少中间页表页，mappages() 会 自动 kalloc() 分配，
        // 直到把第三级 (Level‑0) PTE 填好。
        // 因此 它会生成三级页表——根页表已存在，若分支缺失就动态补齐。
        if(mappages(dst, i, PGSIZE, pa, flags) != 0)
            goto err;
    }
    return 0;
err:
    //解除目标页表中已经映射的页表项
    // uvmunmap()函数就是用来解除映射的。
    // 它会从PGROUNDUP(start)开始，一直解除到出错前的最后一个地址（由'i'记录），
    // 共解除 (i - PGROUNDUP(start)) / PGSIZE 个页。
    uvmunmap(dst, PGROUNDUP(start), (i - PGROUNDUP(start)) / PGSIZE, 0);
    return -1;
}

//与uvmdealloc功能类似，将程序内存从oldsz缩减到newsz，但是不释放实际内存
uint64
cyh_kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if(newsz >= oldsz)
        return oldsz;
    // 2. 核心判断：检查缩减操作是否跨越了至少一个页边界。
    //    PGROUNDUP()是一个宏，将地址向上取整到最近的页边界（4096的倍数）。
    //    - PGROUNDUP(newsz): 是新内存区域结束后的第一个页边界。
    //    - PGROUNDUP(oldsz): 是旧内存区域结束后的第一个页边界。
    //    只有当这两个边界不同时，才意味着至少有一个完整的页可以被解除映射。
    //    例如：oldsz=9000, newsz=7000。PGROUNDUP(oldsz)=12288, PGROUNDUP(newsz)=8192。
    //    此时条件成立，我们需要解除从8192到12288的映射。
    if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
        // 3. 计算需要解除映射的页的数量。
        //    两个边界地址之差就是需要被释放的总内存大小，
        //    再除以PGSIZE（4096），就得到了页的数量。
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        // 4. 调用uvmunmap来执行真正的解除映射操作。
        //    - pagetable: 在这个页表中进行操作。
        //    - PGROUNDUP(newsz): 从这个虚拟地址开始解除映射。
        //    - npages: 解除这么多页。
        //    - 0: 这是一个关键参数，它告诉uvmunmap()函数“不要释放物理内存页”。
        //         uvmunmap内部会检查这个标志，如果为0，它就不会调用kfree()。
        //         kfree是释放物理内存的
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
    }

    return newsz;
}
