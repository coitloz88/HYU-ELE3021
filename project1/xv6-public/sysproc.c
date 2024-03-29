#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// wrapper functions for system calls which are needed for MLFQ scheduling
int sys_yield(void)
{
  yield();
  return 0;
}

int sys_getLevel(void)
{
  return getLevel();
}

int sys_setPriority(void)
{
  int pid = 0, priority = 0;
  if (argint(0, &pid) < 0 || argint(1, &priority) > MAXPRIORITY)
    return -1;

  setPriority(pid, priority);
  return 0;
}

int sys_schedulerLock(void)
{
  int password = 0;
  argint(0, &password);
  if (password != SLPASSWORD)
  {
    struct proc *p = myproc();
    cprintf("[scheduler lock] Wrong Password\n");
    cprintf("pid: %d, time quantum: %d, level of queue: %d\n\n", p->pid, p->execTime, p->qLevel);
    kill(p->pid);
    return -1;
  };

  schedulerLock(password);
  return 0;
}

int sys_schedulerUnlock(void)
{
  int password = 0;
  argint(0, &password);

  if (password != SLPASSWORD)
  {
    struct proc *p = myproc();
    cprintf("[scheduler unlock] Wrong Password\n");
    cprintf("pid: %d, time quantum: %d, level of queue: %d\n\n", p->pid, p->execTime, p->qLevel);
    kill(p->pid);
    return -1;
  };

  schedulerUnlock(password);
  return 0;
}

int sys_printProcessInfo(void)
{
  struct proc *p = myproc();
  printProcess("user program", p);
  return p == 0 ? -1 : 0;
}
