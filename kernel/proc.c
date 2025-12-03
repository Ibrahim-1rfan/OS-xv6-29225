#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// Add near the top with other global variables
struct proc *queues[NQ] = {0};      // Head pointers for each queue
struct proc *queue_tails[NQ] = {0}; // Tail pointers for each queue
int qquantum[NQ] = {1, 2, 4, 8};    // Quantum for each queue level
int boost_interval = 200;           // Boost every 1000 ticks
int last_boost = 0;                 // Last boost time

// === MLFQ FUNCTION DECLARATIONS ===
void enqueue_proc(int level, struct proc *p);
struct proc* dequeue_proc(int level);
int queue_empty(int level);
void boost_priority(void);
int getprocinfo(int pid, struct procinfo *pi);

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // MLFQ initialization
  p->qlevel = 0;
  p->qticks = 0;
  p->next = 0;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  // allocproc returns with p->lock held.
  // To safely enqueue, we must release p->lock,
  // acquire wait_lock, then re-acquire p->lock.
  release(&p->lock);

  acquire(&wait_lock);
  acquire(&p->lock);
  
  p->state = RUNNABLE;
  p->qlevel = 0;
  p->qticks = 0;
  enqueue_proc(0, p);
  
  release(&p->lock);
  release(&wait_lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  // === MLFQ ADDITION: Safe Enqueue ===
  // We need wait_lock to manipulate the process tree and queues safely
  acquire(&wait_lock);
  
  np->parent = p;
  
  // Acquire child lock to change state
  acquire(&np->lock);
  np->state = RUNNABLE;
  
  // Initialize MLFQ stats
  np->qlevel = 0;
  np->qticks = 0;
  
  // Safe to enqueue because we hold wait_lock
  enqueue_proc(np->qlevel, np);
  
  release(&np->lock);
  release(&wait_lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int level;

  c->proc = 0;
  for(;;){
    intr_on();

    // 1. Priority Boosting (Starvation prevention)
    if(ticks - last_boost >= boost_interval) {
      printf("DEBUG: global_ticks reached %d, calling boost\n", boost_interval);
      boost_priority();
      last_boost = ticks;
    }

    int found = 0;

    // 2. MLFQ Queue Scan
    acquire(&wait_lock); 
    for(level = 0; level < NQ; level++) {
      while(!queue_empty(level)) {
        p = dequeue_proc(level);
        
        // Lock process to check state
        acquire(&p->lock);

        if(p->state == RUNNABLE) {
          p->state = RUNNING;
          c->proc = p;
          
          // Release global lock before switching context
          release(&wait_lock);
          
          swtch(&c->context, &p->context);
          
          // === RETURN FROM PROCESS ===
          c->proc = 0;

          // CRITICAL FIX: Lock Ordering
          // We currently hold p->lock (from yield/sched).
          // We CANNOT acquire wait_lock yet because wait_lock > p->lock.
          
          // 1. Release p->lock so we can grab wait_lock safely.
          release(&p->lock);
          
          // 2. Acquire wait_lock (Global Queue Lock)
          acquire(&wait_lock);
          
          // 3. Re-acquire p->lock to verify state and enqueue
          acquire(&p->lock);
          
          // Check if process is still RUNNABLE (it might have yielded)
          if(p->state == RUNNABLE) {
             enqueue_proc(p->qlevel, p);
          }
          
          release(&p->lock);
          // We still hold wait_lock here, ready for next loop iteration
          
          found = 1;
          break; // Break inner loop to restart priority scan from top (level 0)
        } 
        else {
          // Process wasn't runnable (changed state), just release
          release(&p->lock);
        }
      }
      if(found) break; // If we ran a process, restart scan from highest priority
    }
    release(&wait_lock);

    // 3. Emergency Fallback
    // This catches processes that were made RUNNABLE by wakeup/kill
    // but were not added to the queue to avoid lock panics.
    if(!found) {
      for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state == RUNNABLE) {
          // Found a stray runnable process
          p->state = RUNNING;
          c->proc = p;
          
          swtch(&c->context, &p->context);
          
          c->proc = 0;
          
          // It returned. We must enqueue it now if it's still RUNNABLE.
          if(p->state == RUNNABLE) {
            // Apply safe lock ordering: release p -> get wait -> get p
            release(&p->lock); 
            
            acquire(&wait_lock);
            acquire(&p->lock);
            
            if(p->state == RUNNABLE) {
              enqueue_proc(p->qlevel, p);
            }
            
            release(&wait_lock);
            // We hold p->lock, but the loop continues with p increment
            // We can release it here to be safe
          }
          found = 1;
        }
        release(&p->lock);
      }
      
      if(!found) {
        intr_on();
        asm volatile("wfi");
      }
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);

  // Note: We do NOT promote or enqueue here.
  // We simply set state to RUNNABLE.
  // The scheduler loop will handle re-enqueueing upon return.
  p->state = RUNNABLE;
  
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    fsinit(ROOTDEV);
    first = 0;
    __sync_synchronize();
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);

  p->chan = chan;
  p->state = SLEEPING;

  p->qticks = 0;

  sched();

  p->chan = 0;

  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        // DO NOT ENQUEUE HERE.
        // We let the scheduler "fallback" find this process.
        // This avoids the panic: acquire error (recursive locks).
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        // DO NOT ENQUEUE HERE.
        // Relies on scheduler fallback to avoid lock ordering panic.
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// Queue operations
void
enqueue_proc(int level, struct proc *p)
{
  p->next = 0;
  if (!queues[level]) {
    queues[level] = p;
    queue_tails[level] = p;
  } else {
    queue_tails[level]->next = p;
    queue_tails[level] = p;
  }
}

struct proc*
dequeue_proc(int level)
{
  struct proc *p = queues[level];
  if (!p) return 0;

  queues[level] = p->next;
  if (!queues[level])
    queue_tails[level] = 0;

  p->next = 0;
  return p;
}

int
queue_empty(int level)
{
  return queues[level] == 0;
}

void
boost_priority(void)
{
  struct proc *p;
  int count = 0;

  // We must acquire wait_lock to manipulate queues safely
  acquire(&wait_lock); 
  
  // Clear all queues first
  for(int i = 0; i < NQ; i++) {
     queues[i] = 0;
     queue_tails[i] = 0;
  }

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);

    // Count only Used processes that are getting boosted
    if(p->state != UNUSED && p->qlevel > 0) {
        count++;
    }
    if(p->state == RUNNABLE || p->state == RUNNING) {
      p->qlevel = 0;
      p->qticks = 0;
      // If it's runnable, we must re-add it to the top queue
      // If it's running, it will be added when it yields
      if(p->state == RUNNABLE) {
        enqueue_proc(0, p);
      }
    }
    release(&p->lock);
  }
  
  release(&wait_lock);
printf(">>>>>> BOOST: Reset %d processes to priority 0 (global_ticks=0) <<<<<<\n", count);
}

int
getprocinfo(int pid, struct procinfo *pi)
{
  struct proc *p;
  int found = 0;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED) {
      pi->pid = p->pid;
      pi->state = p->state;
      pi->qlevel = p->qlevel;
      pi->qticks = p->qticks;
      safestrcpy(pi->name, p->name, sizeof(pi->name));
      release(&p->lock);
      found = 1;
      return 0;
    }
    release(&p->lock);
    if(found) return 0;
  }
  return -1;
}
