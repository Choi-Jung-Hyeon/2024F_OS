#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#include "sleeplock.h"	//pa3
#include "fs.h"
#include "file.h"

#define MMAPBASE 0x40000000

struct mmap_area area[64];

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

//proj2
int ticks_total = 0;
int weight[40] = {
  /*   0*/ 88761,   71755,    56483,    46273,    36291,
  /*   5*/ 29154,   23254,    18705,    14949,    11916,
  /*  10*/ 9548,    7620,     6100,     4904,     3906,
  /*  15*/ 3121,    2501,     1991,     1586,     1277,
  /*  20*/ 1024,    820,      655,      526,      423,
  /*  25*/ 335,     272,      215,      172,      137,
  /*  30*/ 110,     87,       70,       56,       45,
  /*  35*/ 36,      29,       23,       18,       15
};

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority = 20;		//proj1
  p->runtime = 0;		//proj2
  p->vruntime = 0;		//proj2
  p->timeslice = 0;		//proj2
  p->runtime_div_weight = 0;	//proj2

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 33
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  
  
  p->state = RUNNABLE;
  
  /*
  p->priority = 20;	//proj1
  p->runtime = 0;	//proj2
  p->vruntime = 0;	//proj2
  */

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->vruntime = curproc->vruntime;
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  //proj2
  //np->priority = curproc->priority;
  //np->runtime = curproc->runtime;
  //np->vruntime = curproc->vruntime;

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *proc;
  struct proc *proc1;
  struct proc *proc2;
  struct proc *most_proc;
  int weight_total;
  struct cpu *c = mycpu();
  c-> proc = 0;

  for(;;){
    sti();
    acquire(&ptable.lock);
    for(proc = ptable.proc; proc < &ptable.proc[NPROC]; proc++){
      if(proc->state != RUNNABLE){
        continue;
      }

      weight_total = 0;
      for(proc1 = ptable.proc; proc1 < &ptable.proc[NPROC]; proc1++){
        if(proc1->state != RUNNABLE){
          continue;
	}
	weight_total += weight[proc1->priority];
      }

      most_proc = proc;
      for(proc2 = ptable.proc; proc2 < &ptable.proc[NPROC]; proc2++){
        if(proc2->state != RUNNABLE){
          continue;
	}
	if(most_proc->vruntime > proc2->vruntime){
	  most_proc = proc2;
	}
      }

      most_proc->timeslice = ((10 * weight[most_proc->priority]) / weight_total) + ((10 * weight[most_proc->priority]) % weight_total != 0);
      //most_proc->timeslice = ((10 * weight[most_proc->priority]) / weight_total);

      c->proc = most_proc;
      switchuvm(most_proc);
      most_proc->state = RUNNING;

      swtch(&(c->scheduler), most_proc->context);
      switchkvm();
      c->proc = 0;
    }
    release(&ptable.lock);
  }

/*
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    //proj2
    int vruntime_min = 2147483647;
    struct proc *vruntime_min_p = 0;
    int runnable = 0;
    int weight_total = 0;

    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state != RUNNABLE) {
        continue;
      }
      
      if(p->vruntime < vruntime_min) {
        vruntime_min = p->vruntime;
	vruntime_min_p = p;
      }

      runnable = 1;
      weight_total += weight[p->priority];
    }

    if(runnable == 1) {
      vruntime_min_p->timeslice = 1000 * 10 * (weight[vruntime_min_p->priority] / weight_total);

      c->proc = vruntime_min_p;
      switchuvm(vruntime_min_p);
      vruntime_min_p->state = RUNNING;

      swtch(&(c->scheduler), vruntime_min_p->context);
      switchkvm();
      c->proc = 0;
    }
    release(&ptable.lock);

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
  */	
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
 
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  int vrun_min = 0;
  int run = 0;
  int vrun = 0;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE){
      run = 1;
      vrun_min = p->vruntime;
    }

    if(run == 1){
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == RUNNABLE){
	  if(vrun_min > p->vruntime){
	    vrun_min = p->vruntime;
	  }
	}
      }
    }

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == SLEEPING && p->chan == chan){
        vrun = ((1000 * 1024) / weight[p->priority]);
	if(vrun_min < vrun){
	  p->vruntime = 0;
	}
	else{
	  p->vruntime = vrun_min - vrun;
	}
	p->state = RUNNABLE;
      }
    }
  }
/*
  struct proc *p;

  int runnable = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE) {
      runnable = 1;
      break;
    }
  }

  if(runnable == 0) {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state == SLEEPING && p->chan == chan) {
        p->vruntime = 0;
	p->state = RUNNABLE;
      }
    }
  }
  else {
    int vruntime_min = __INT32_MAX__;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state == RUNNABLE && p->vruntime < vruntime_min) {
        vruntime_min = p->vruntime;
      }
    }

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state == SLEEPING && p->chan == chan) {
        p->vruntime = vruntime_min - (1000 * weight[20] / weight[p->priority]);
	p->state = RUNNABLE;
      }
    }
  }

  
  //for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  //  if(p->state == SLEEPING && p->chan == chan)
  //    p->state = RUNNABLE;
  
*/
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
getpname(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      cprintf("%s\n", p->name);
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
getnice(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      int nice_value = p->priority;
      release(&ptable.lock);
      return nice_value;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
setnice(int pid, int value)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      if(0 <= value && value <= 39){
        p->priority = value;
        release(&ptable.lock);
        return 0;
      }
      else{
        //p->priority = 20;
        release(&ptable.lock);
        return -1;
      }
    }
  }
  release(&ptable.lock);
  return -1;
}

void
ps(int pid)
{
  struct proc *p;
  char pstate_str[10];

  acquire(&ptable.lock);
  cprintf("name\t pid\t state\t\t priority\t runtime/weight\t runtime\t vruntime\t tick ");
  cprintf("%d000\n", ticks_total);
  //cprintf("%d000\n", ticks);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(pid == 0){
      enum procstate pstate = p->state;
      switch(pstate){
        case 0:
	  strncpy(pstate_str, "UNUSED\0", 7);
	  break;
	case 1:
	  strncpy(pstate_str, "EMBRYO\0", 7);
	  break;
	case 2:
	  strncpy(pstate_str, "SLEEPING\0", 9);
	  break;
	case 3:
	  strncpy(pstate_str, "RUNNABLE\0", 9);
	  break;
	case 4:
	  strncpy(pstate_str, "RUNNING\0", 8);
	  break;
	case 5:
	  strncpy(pstate_str, "ZOMBIE\0", 7);
	  break;
      }
      if(pstate != 0){
	cprintf("%s\t ", p->name);
        cprintf("%d\t ", p->pid);
        cprintf("%s\t ", pstate_str);
        cprintf("%d\t\t ", p->priority);
        cprintf("%d\t\t ", p->runtime_div_weight);
        cprintf("%d\t\t ", p->runtime);
        cprintf("%d\n", p->vruntime);
      }
    }
    else if(pid == p->pid){
      enum procstate pstate = p->state;
      switch(pstate){
        case 0:
          strncpy(pstate_str, "UNUSED\0", 7);
          break;
        case 1:
          strncpy(pstate_str, "EMBRYO\0", 7);
          break;
        case 2:
          strncpy(pstate_str, "SLEEPING\0", 9);
          break;
        case 3:
          strncpy(pstate_str, "RUNNABLE\0", 9);
          break;
        case 4:
          strncpy(pstate_str, "RUNNING\0", 8);
          break;
        case 5:
          strncpy(pstate_str, "ZOMBIE\0", 7);
          break;
      }
      if(pstate != 0){
	cprintf("%s\t ", p->name);
        cprintf("%d\t ", p->pid);
        cprintf("%s\t ", pstate_str);
        cprintf("%d\t\t ", p->priority);
	cprintf("%d\t\t ", p->runtime_div_weight);
	cprintf("%d\t\t ", p->runtime);
	cprintf("%d\n", p->vruntime);
      }
      break;
    }
    else
      continue;
  }
  release(&ptable.lock);
}

uint
mmap(uint addr, int length, int prot, int flags, int fd, int offset)
{
  struct proc* p = myproc();
  uint start = addr + MMAPBASE;

  struct file* f = 0;
  if(fd != -1){
    f = p->ofile[fd];
  }

  int Anonymous = 0;
  int Populate = 0;
  int Prot_Read = 0;
  int Prot_Write = 0;
  char* memory = 0;

  if(flags & MAP_ANONYMOUS){
    Anonymous = 1;
  }
  if(flags & MAP_POPULATE){
    Populate = 1;
  }
  if(prot & PROT_READ){
    Prot_Read = 1;
  }
  if(prot & PROT_WRITE){
    Prot_Write = 1;
  }

  if( !(Anonymous) && (fd == 1) ){
    return 0;
  }
  if(f != 0){
    if( !(f->readable) && Prot_Read){
      return 0;
    }
    if( !(f->writable) && Prot_Write){
      return 0;
    }
  }

  int i = 0;
  while(area[i].area_valid != 0){
    i++;
  }
  if(f){
    f = filedup(f);
  }

  area[i].f = f;
  area[i].addr = start;
  area[i].length = length;
  area[i].offset = offset;
  area[i].prot = prot;
  area[i].flags = flags;
  area[i].p = p;
  area[i].area_valid = 1;

  //Page fault
  if( !(Anonymous) && !(Populate) ){
    return start;
  }
  if( Anonymous && !(Populate) ){
    return start;
  }
  if( !(Anonymous) && Populate ){
    f->off = offset;
    uint _ptr = 0;

    for(_ptr = start; _ptr < start + length; _ptr += PGSIZE){
      memory = kalloc();
      if( !(memory) ){
        return 0;
      }
      memset(memory, 0, PGSIZE);
      fileread(f, memory, PGSIZE);
      int perm = prot | PTE_U;
      int rt = mappages(p->pgdir, (void*)(_ptr), PGSIZE, V2P(memory), perm);
      if(rt == -1){
        return 0;
      }
    }
    return start;
  }

  if( Anonymous && Populate){
    uint _ptr = 0;
    for(_ptr = start; _ptr < start + length; _ptr += PGSIZE){
      memory = kalloc();
      if( !(memory) ){
	return 0;
      }
      memset(memory, 0, PGSIZE);
      int perm = prot | PTE_U;
      int rt = mappages(p->pgdir, (void*)(_ptr), PGSIZE, V2P(memory), perm);
      if(rt == -1){
	return 0;
      }
    }
    return start;
  }
  return start;
}    
