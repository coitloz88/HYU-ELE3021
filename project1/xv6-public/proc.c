#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct
{
  struct spinlock lock;
  struct proc *proc;
} ltable;

struct
{
  struct spinlock lock;
  struct mlfQueue mlfQueue[MAXQLEVEL];
} qtable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&ltable.lock, "ltable");
}

// init_queue function and call it in main() function to ensure queue is initialized before it is used in scheduler()
void qinit(void)
{
  initlock(&qtable.lock, "qtable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->arrivedTime = p->pid;
  p->priority = MAXPRIORITY - 1;
  p->execTime = 0;
  p->isLock = UNLOCKED;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// method that prints process info for debugging
void printProcess(const char *funcName, struct proc *targetProc)
{
  if (targetProc == 0)
    cprintf("\n[%s log] process is NULL!\n", funcName);
  else
    cprintf("\n[%s log] pid: %d, qLevel: %d, state: %d, arrivedTime: %d, execTime: %d, priority: %d, isLock: %d\n", funcName, targetProc->pid, targetProc->qLevel, targetProc->state, targetProc->arrivedTime, targetProc->execTime, targetProc->priority, targetProc->isLock);
}

// enqueue in mlfQueue: ptable을 잡은 함수에서만 호출가능
int MLFQenqueue(struct proc *p, int qLevel)
{
  if (qLevel < 0 || qLevel >= MAXQLEVEL)
    return -3; // invalid queue level input
  else if (qtable.mlfQueue[qLevel].rear >= NPROC)
    return -2; // queue already has more than max process number(NPROC)
  else if (p != 0 && p->state != RUNNABLE)
    return -1;

  // enqueue logic starts here
  acquire(&qtable.lock);

  p->qLevel = qLevel;
  qtable.mlfQueue[qLevel].procsQueue[qtable.mlfQueue[qLevel].rear] = p;
  qtable.mlfQueue[qLevel].rear += 1;

  release(&qtable.lock);
  return 0;
}

int MLFQfrontEnqueue(struct proc *p, int qLevel)
{
  if (qLevel < 0 || qLevel >= MAXQLEVEL)
    return -3; // invalid queue level input
  else if (qtable.mlfQueue[qLevel].rear >= NPROC)
    return -2; // queue already has more than max process number(NPROC)
  else if (p != 0 && p->state != RUNNABLE)
    return -1;

  // enqueue logic starts here
  acquire(&qtable.lock);

  for (int i = qtable.mlfQueue[qLevel].rear - 1; i > 0; i--)
  {
    qtable.mlfQueue[qLevel].procsQueue[i + 1] = qtable.mlfQueue[qLevel].procsQueue[i];
  }

  p->qLevel = qLevel;

  qtable.mlfQueue[qLevel].procsQueue[0] = p;
  qtable.mlfQueue[qLevel].rear += 1;

  release(&qtable.lock);
  return 0;
}

int findFromQueueByPid(int pid, struct qLocation *qLoc)
{
  struct proc *p = 0;

  // find process in mlfq table
  int foundQLevel = -1;
  int foundQIndex = -1;
  int foundQRear = -1;

  for (int qLevel = 0; qLevel < MAXQLEVEL; qLevel++)
  {
    int qEntriesCnt = qtable.mlfQueue[qLevel].rear;
    for (int qIndex = 0; qIndex < qEntriesCnt; qIndex++)
    {
      if (pid == qtable.mlfQueue[qLevel].procsQueue[qIndex]->pid)
      {
        p = qtable.mlfQueue[qLevel].procsQueue[qIndex];
        foundQLevel = qLevel;
        foundQIndex = qIndex;
        foundQRear = qEntriesCnt;
        break;
      }
    }
    if (p != 0)
      break;
  }

  if (p == 0 || foundQIndex < 0 || foundQLevel < 0 || foundQRear < 0)
    return -1;
  else
  {
    qLoc->qLevel = foundQLevel;
    qLoc->qIndex = foundQIndex;
    qLoc->qRear = foundQRear;
  }
  return 0;
}

int MLFQdeleteByPid(int pid)
{
  struct qLocation qLoc = {0, 0, 0};
  if (findFromQueueByPid(pid, &qLoc) == -1)
    return -1;

  int foundQLevel = qLoc.qLevel;
  int foundQIndex = qLoc.qIndex;
  int foundQRear = qLoc.qRear;

  acquire(&qtable.lock);

  // delete process from mlfq table
  for (int qIter = foundQIndex; qIter < foundQRear - 1; qIter++)
  {
    qtable.mlfQueue[foundQLevel].procsQueue[qIter] = qtable.mlfQueue[foundQLevel].procsQueue[qIter + 1];
  }
  qtable.mlfQueue[foundQLevel].procsQueue[qtable.mlfQueue[foundQLevel].rear - 1] = 0;

  qtable.mlfQueue[foundQLevel].rear -= 1;

  release(&qtable.lock);

  return 0;
}

struct proc *MLFQfirstProc(int qLevel)
{
  if (qLevel < 0 || qLevel >= MAXQLEVEL)
    return 0; // invalid queue level input, return null
  else if (qtable.mlfQueue[qLevel].rear == 0)
    return 0; // queue does not have any process, return null

  struct proc *p = 0;
  if (qLevel < MAXQLEVEL - 1)
  {
    p = qtable.mlfQueue[qLevel].procsQueue[0];
  }
  else
  {
    int dequeueIndex = -1;
    int minArrivedTime = 0;

    for (int iterPrior = 0; iterPrior < MAXPRIORITY && dequeueIndex == -1; iterPrior++)
    {
      minArrivedTime = qtable.mlfQueue[BOTTOM].procsQueue[0]->arrivedTime;
      for (int i = 0; i < qtable.mlfQueue[BOTTOM].rear; i++)
      {
        // 1. priority가 높은 것 우선
        // 2. FCFS: arrivedTime이 짧은 것 우선
        if (qtable.mlfQueue[BOTTOM].procsQueue[i]->priority == iterPrior && qtable.mlfQueue[BOTTOM].procsQueue[i]->arrivedTime < minArrivedTime)
        {
          dequeueIndex = i;
          break;
        };
      }
      if (minArrivedTime == qtable.mlfQueue[BOTTOM].procsQueue[0]->arrivedTime)
        dequeueIndex = 0;
    }

    p = qtable.mlfQueue[BOTTOM].procsQueue[dequeueIndex];
  }
  return p;
}

struct proc *MLFQdequeue(int qLevel)
{
  if (qLevel < 0 || qLevel >= MAXQLEVEL)
    return 0; // invalid queue level input, return null
  else if (qtable.mlfQueue[qLevel].rear == 0)
    return 0; // queue does not have any process, return null

  // dequeue logic starts here

  struct proc *p = 0;

  if (qLevel < MAXQLEVEL - 1)
  {
    acquire(&qtable.lock);

    p = qtable.mlfQueue[qLevel].procsQueue[0];

    // shift queue contents left to prevent overflow
    for (int i = 0; i < qtable.mlfQueue[qLevel].rear - 1; i++)
    {
      qtable.mlfQueue[qLevel].procsQueue[i] = qtable.mlfQueue[qLevel].procsQueue[i + 1];
    }
    qtable.mlfQueue[qLevel].procsQueue[qtable.mlfQueue[qLevel].rear - 1] = 0;
    qtable.mlfQueue[qLevel].rear -= 1; // dequeue and shift is over, decrease rear

    release(&qtable.lock);
  }
  else
  {
    // qLevel = BOTTOM, priority scheduling
    acquire(&qtable.lock);

    int dequeueIndex = -1;
    int minArrivedTime = 0;

    for (int iterPrior = 0; iterPrior < MAXPRIORITY && dequeueIndex == -1; iterPrior++)
    {
      minArrivedTime = qtable.mlfQueue[BOTTOM].procsQueue[0]->arrivedTime;
      for (int i = 0; i < qtable.mlfQueue[BOTTOM].rear; i++)
      {
        if (qtable.mlfQueue[BOTTOM].procsQueue[i]->priority == iterPrior && qtable.mlfQueue[BOTTOM].procsQueue[i]->arrivedTime < minArrivedTime)
        {
          dequeueIndex = i;
          break;
        };
      }
      if (minArrivedTime == qtable.mlfQueue[BOTTOM].procsQueue[0]->arrivedTime)
        dequeueIndex = 0;
    }

    p = qtable.mlfQueue[BOTTOM].procsQueue[dequeueIndex];

    // shift queue contents left to prevent overflow
    for (int i = dequeueIndex; i < qtable.mlfQueue[BOTTOM].rear - 1; i++)
    {
      qtable.mlfQueue[BOTTOM].procsQueue[i] = qtable.mlfQueue[BOTTOM].procsQueue[i + 1];
    }
    qtable.mlfQueue[BOTTOM].procsQueue[qtable.mlfQueue[BOTTOM].rear - 1] = 0;

    qtable.mlfQueue[BOTTOM].rear -= 1;

    release(&qtable.lock);
  }

  return p;
}

void increaseExecTime(struct proc *p)
{
  acquire(&ptable.lock);
  p->execTime = p->execTime + 1;
  release(&ptable.lock);
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
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
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  MLFQenqueue(p, TOP);

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  np->arrivedTime = pid;
  np->execTime = 0;
  np->qLevel = TOP;
  np->priority = MAXPRIORITY - 1;
  np->isLock = UNLOCKED;

  MLFQenqueue(np, TOP);

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // if locked process is exiting, call schedulerLockDone() to wrapup
  if (curproc->isLock == LOCKED)
  {
    schedulerLockDone(1);
  }

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
  MLFQdeleteByPid(curproc->pid);

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
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
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
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
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

struct proc *schedulerChooseProcess(int qLevel)
{
  struct proc *targetProc = MLFQfirstProc(qLevel);
  // TODO: dequeue by priority when qLevel is BOTTOM

  while (targetProc != 0)
  {
    if (targetProc->state != RUNNABLE || targetProc->killed == 1)
    {
      // deprecated process(state is not RUNNABLE or killed process) is kicked out of the queue

      // before kick out, init some process variables related to queue
      targetProc->execTime = 0;
      targetProc->priority = MAXPRIORITY - 1;

      // TODO: enqueue initialized proc?
      //  No, 만약 enqueue하게 되면 RUNNABLE 하지 않은 프로세스들로 ptable이 가득 찬 경우 무한 루프를 돌게됨
      //  RUNNABLE 하지 않거나 killed된 process들은 재실행시 다시 enqueue 하도록...
      MLFQdequeue(qLevel);
      targetProc = MLFQfirstProc(qLevel);
      continue;
    }

    // only RUNNABLE process arrives in this conditional statement
    if (targetProc->execTime >= TIME_QUANTUM(qLevel))
    {
      targetProc = MLFQdequeue(qLevel);
      targetProc->execTime = 0;
      if (qLevel < BOTTOM)
      {
        // qLevel = TOP, qLevel = MIDDLE
        MLFQenqueue(targetProc, qLevel + 1);
      }
      else
      {
        if (targetProc->priority > 0)
          targetProc->priority -= 1;
        MLFQenqueue(targetProc, qLevel);
      }
      targetProc = MLFQfirstProc(qLevel);
      continue;
    }

    // valid process arrives in the end of loop
    // not NULL, RUNNABLE, execTime is valid
    break;
  }

  // NULL(means no process in current level of queue) or valid process is returned
  return targetProc;
}

int isValidProcess(struct proc *p)
{
  if (p != 0 && p->state == RUNNABLE)
  {
    return 1;
  };
  return 0;
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();
    struct proc *targetProc = 0;

    acquire(&ltable.lock);
	targetProc = ltable.proc;
    release(&ltable.lock);

    if (targetProc != 0)
    {
      if (targetProc->state != RUNNABLE || targetProc->killed == 1)
      {
        schedulerLockDone(0);
        continue;
      }
      
	  // locked process exists
      c->proc = targetProc;
      switchuvm(targetProc);

      acquire(&ptable.lock);
      targetProc->state = RUNNING;

      swtch(&(c->scheduler), targetProc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      release(&ptable.lock);
    }
    // Loop over process table looking for process to run.
    else
    {
      acquire(&ptable.lock);

      for (int qLevel = 0; qLevel < MAXQLEVEL; qLevel++)
      {
        targetProc = 0;

        // TODO: check for higher level queue has new arrived RUNNABLE process
        for (int prev = 0; prev <= qLevel; prev++)
        {
          targetProc = schedulerChooseProcess(prev);

          if (isValidProcess(targetProc))
          {
            qLevel = prev;
            break;
          }
        }

        if (!isValidProcess(targetProc) || targetProc->execTime >= TIME_QUANTUM(qLevel))
          continue;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = targetProc;
        switchuvm(targetProc);
        targetProc->state = RUNNING;

        swtch(&(c->scheduler), targetProc->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }

      release(&ptable.lock);
    }

	/*
    if (targetProc == 0 && ltable.proc == 0)
    {
      // Missing process to run
      // Find RUNNABLE process in ptable
      acquire(&ptable.lock);

      for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if (p->state != RUNNABLE || p->killed == 1)
          continue;

        // check for same process is already in MLFQ
        struct qLocation qLoc = {0, 0, 0};
        if (findFromQueueByPid(p->pid, &qLoc) == 0)
          continue;

        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        c->proc = 0;
        break;
      }

      release(&ptable.lock);
    }
	*/
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  // yield가 호출되는 상황
  // 1. timer interrupt에 의해 호출: 1tick 실행되었다는 뜻이므로, process의 exectime을 증가
  // 2. 강제로 외부에서 systemcall로 호출: CPU를 잡고 있는 process가 실행되다가 yield

  acquire(&ptable.lock);      // DOC: yieldlock
  myproc()->state = RUNNABLE; // 실행되던 프로세스를 다시 RUNNING에서 RUNNABLE로 바꿔줌

  if (myproc()->isLock == UNLOCKED)
  {
    MLFQdequeue(myproc()->qLevel);
    MLFQenqueue(myproc(), myproc()->qLevel);
  }
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
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
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  if (p->isLock == LOCKED)
  {
    schedulerLockDone(1);
  }

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
    {
      p->state = RUNNABLE;
      p->execTime = 0;
      p->priority = MAXPRIORITY - 1;
      MLFQenqueue(p, 0);
    }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      p->execTime = 0;
      p->priority = MAXPRIORITY - 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);

  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int getLevel(void)
{
  return myproc()->qLevel;
}

void setPriority(int pid, int priority)
{
  if (priority < 0 || priority > MAXPRIORITY)
    return; // priority can be 0~3 value

  acquire(&ptable.lock);

  int found = 0;
  for (int qLevel = 0; qLevel < MAXQLEVEL && !found; qLevel++)
  {
    for (int i = 0; i < qtable.mlfQueue[qLevel].rear; i++)
    {
      if (qtable.mlfQueue[qLevel].procsQueue[i]->pid == pid)
      {
        qtable.mlfQueue[qLevel].procsQueue[i]->priority = priority;
        found = 1;
        break;
      };
    }
  }

  release(&ptable.lock);
}

void priorityBoosting(void)
{
  acquire(&ptable.lock);

  // TOP queue에 있는 process의 exectime과 priority 초기화
  int TopRear = qtable.mlfQueue[TOP].rear;
  for (int iter = 0; iter < TopRear; iter++)
  {
    struct proc *p = qtable.mlfQueue[TOP].procsQueue[iter];
    if (p != 0)
    {
      p->execTime = 0;
      p->priority = MAXPRIORITY - 1;
    }
  }

  // MIDDLE, BOTTOM에 있는 프로세스 초기화 후 TOP에 enqueue
  for (int qLevel = MIDDLE; qLevel < MAXQLEVEL; qLevel++)
  {
    struct proc *p = MLFQdequeue(qLevel);

    while (p != 0)
    {
      p->execTime = 0;
      p->priority = MAXPRIORITY - 1;
      MLFQenqueue(p, TOP);
      p = MLFQdequeue(qLevel);
    }
  }

  release(&ptable.lock);

  // priority boosting에 따른 tick 초기화
  acquire(&tickslock);
  ticks = 0;
  release(&tickslock);
}

void schedulerLock(int password)
{
  // kill process and return if password is wrong
  struct proc *curproc = myproc();

  if (password != SLPASSWORD)
  {
    cprintf("[scheduler lock] Wrong Password!\n");
    cprintf("pid: %d, time quantum: %d, level of queue: %d\n\n", curproc->pid, curproc->execTime, curproc->qLevel);
    kill(curproc->pid);
    return;
  };

  int pid = curproc->pid;
  int found = 0;
  for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (pid == p->pid)
    {
      found = 1;
      break;
    }
  }
  // 기존에 존재하던 프로세스가 아니라면, schedulerLock을 호출할 수 없다.
  if (found == 0)
    return;

  // 이미 lock이 걸린 process가 있으면 종료한다.
  if (ltable.proc != 0)
    return;

  MLFQdeleteByPid(pid); // lock된 프로세스는 MLFQ에서 빠져나와서 ltable에 존재하다가 unlock되면 MLFQ로 돌아간다.

  acquire(&ptable.lock);
  curproc->isLock = LOCKED; // 현재 프로세스를 LOCKED로 바꿔줌
  curproc->state = RUNNABLE; // sched 호출을 위해 RUNNABLE로 바꿔줌
  release(&ptable.lock);

  acquire(&ltable.lock);
  ltable.proc = curproc; // ltable의 proc을 설정
  release(&ltable.lock);

  // lock 성공, initialize global tick to 0
  acquire(&tickslock);
  ticks = 0;
  release(&tickslock);

  acquire(&ptable.lock);
  sched();
  release(&ptable.lock);
}

void schedulerLockDone(int isExit)
{
  struct proc *lockproc = ltable.proc;

  acquire(&ltable.lock);
  ltable.proc = 0;
  release(&ltable.lock);

  if (lockproc == 0)
  {
    return;
  }

  acquire(&ptable.lock);
  lockproc->execTime = 0;
  lockproc->priority = MAXPRIORITY - 1;
  lockproc->isLock = UNLOCKED;

  enum procstate curState = RUNNABLE;
  if (lockproc->state != RUNNING || lockproc->state != RUNNABLE)
    curState = lockproc->state;
  lockproc->state = curState;

  if (!isExit)
    MLFQfrontEnqueue(lockproc, TOP);
  release(&ptable.lock);
}

void schedulerUnlock(int password)
{
  if (password != SLPASSWORD)
  {
    struct proc *p = myproc();
    cprintf("[scheduler unlock] Wrong Password!\n");
    cprintf("pid: %d, time quantum: %d, level of queue: %d\n\n", p->pid, p->execTime, p->qLevel);
    kill(p->pid);
    return;
  };

  // TODO: lock되어있는 프로세스가 실행되는 동안은 다른 프로세스는 동작 못하고 있는 건가? 그럼 unlock함수는 무조건 lock된 프로세스(본인)가 호출하는건가? 다른 프로세스에서 호출 가능
  // TODO: kill process and enqueue in the rear of queue(use original funtion)

  if (ltable.proc == 0)
  {
    return;
  }

  schedulerLockDone(0);
}
