// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

struct {
	struct spinlock lock;
	char* bitmap;
	int num_free_blocks;
} swap_tool;

struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;
struct spinlock PGlock;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  num_free_pages = 0;
  kmem.use_lock = 0;

  for(int i = 0; i < PHYSTOP / PGSIZE; i++){
	  pages[i].vaddr = 0;
	  pages[i].pgdir = 0;
	  pages[i].prev = 0;
	  pages[i].next = 0;
  }

  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;


  initlock(&PGlock, "PGlock");
  page_lru_head = 0;
  num_lru_pages = 0;

  initlock(&swap_tool.lock, "swap_tool");
  swap_tool.bitmap = kalloc();
  if( !swap_tool.bitmap )
	  panic("null");

  acquire(&swap_tool.lock);
  swap_tool.num_free_blocks = SWAPMAX / (PGSIZE / 512);
  memset((void*)swap_tool.bitmap, 0, PGSIZE);

  swap_tool.bitmap[SWAPMAX / 8 / (PGSIZE / 512)] |= 1 << (SWAPMAX % 8);
  release(&swap_tool.lock);
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

	num_free_pages++;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(!r){
	  if(reclaim()) goto try_again;
	  else{
		  cprintf("out of memory\n");
		  return 0;
	  }
  }
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  
  num_free_pages--;
  return (char*)r;
}

int
reclaim(void){
	if(page_lru_head == 0){
		cprintf("no pages in LRU\n");
		return 0;
	}
	if(num_lru_pages == 0){
		cprintf("no pages in LRU\n");
		return 0;
	}

	release(&kmem.lock);
	acquire(&PGlock);

	pte_t* PTE = 0;
	struct page* problem = page_lru_head;
	

	for(;;){
		PTE = walkpgdir(problem->pgdir, (void*)problem->vaddr, 0);

		if( !(PTE_U & *PTE) )
			del_lru(P2V(PTE_ADDR(*PTE)));
		else if(PTE_A & *PTE){
			*PTE &= ~PTE_A;
			page_lru_head = problem->next;
			problem = problem->next;
		}
		else
			break;
	}
	release(&PGlock);

	int box = get_box();
	if(box == -1)
		return 0;
	
	swapwrite(P2V(PTE_ADDR(*PTE)), box);
	del_lru(P2V(PTE_ADDR(*PTE)));
	kfree(P2V(PTE_ADDR(*PTE)));

	int bit = 12;
	*PTE = ((~PTE_P) & (0xfff & *PTE)) | (box << bit);
	return 1;
}

int
get_box(void){
	if(swap_tool.num_free_blocks == 0)
		return -1;

	acquire(&swap_tool.lock);

	int id;
	int index = SWAPMAX / 8 / (PGSIZE / 512);
	for(id = 0; id < index; id++){
		if(swap_tool.bitmap[id] != (char)0xff)
			break;
	}

	if(id == index){
		release(&swap_tool.lock);
		return -1;
	}

	for(int i = 0; i < 8; i++){
		if( (1 << i) & !(swap_tool.bitmap[id] )){
			swap_tool.bitmap[id] |= (1 << i);
			release(&swap_tool.lock);
			return id * 8 + i;
		}
	}
	release(&swap_tool.lock);
	return -1;
}

void
set_bitmap(int box){
	acquire(&swap_tool.lock);

	
	int i = box / 8;
	int j = box % 8;
	swap_tool.bitmap[i+0] |= (1 << j);
	swap_tool.num_free_blocks--;

	release(&swap_tool.lock);
}

void
del_bitmap(int box){
	acquire(&swap_tool.lock);

	
	int i = box / 8;
	int j = box % 8;
	swap_tool.bitmap[i+0] &= ~(1 << j);
	swap_tool.num_free_blocks++;

	release(&swap_tool.lock);
}

void
set_lru(pde_t* pgdir, char* p_addr, char* v_addr){
	int bit = 12;
	uint index = V2P(p_addr) >> bit;

	acquire(&PGlock);
	num_free_pages--;
	num_lru_pages++;

	pages[index].vaddr = v_addr;
	pages[index].pgdir = pgdir;
	

	if(page_lru_head != 0){
		pages[index].next = page_lru_head;
		pages[index].prev = page_lru_head->prev;
		page_lru_head->prev = &pages[index];
		pages[index].prev->next = &pages[index];
		page_lru_head = &pages[index];
	}
	if( !page_lru_head ){
		page_lru_head = &pages[index];
		pages[index].next = &pages[index];
		pages[index].prev = &pages[index];
	}

	release(&PGlock);
}

void
del_lru(char* p_addr){
	int bit = 12;
	uint index = V2P(p_addr) >> bit;

	acquire(&PGlock);

	if( !pages[index].next ){
		release(&PGlock);
		return;
	}
	if( !pages[index].prev ){
		release(&PGlock);
		return;
	}

	num_free_pages++;
	num_lru_pages--;

	if(page_lru_head == &pages[index]){
		page_lru_head = pages[index].next;
		pages[index].prev->next = pages[index].next;
		pages[index].next->prev = pages[index].prev;
	}
	else if(page_lru_head->prev == &pages[index]){
		page_lru_head->prev = pages[index].prev;
		pages[index].prev->next = pages[index].next;
		pages[index].next->prev = pages[index].prev;
	}	
	else if(pages[index].next == &pages[index] || num_lru_pages == 1){
		page_lru_head->prev = 0;
		page_lru_head->next = 0;
		page_lru_head = 0;
	}
	else{
		pages[index].prev->next = pages[index].next;
		pages[index].next->prev = pages[index].prev;
	}

	memset( (void*)&pages[index], 0, sizeof(struct page));
	release(&PGlock);
	return;
}
