#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixed_point.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif


/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes sleeping in THREAD_BLOCKED state, that is, processes
   that are waiting for an alarm before retuning to ready_list. */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* System-wide load_avg */
int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */

/* This is 2016 spring cs330 skeleton code */

void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Initialize system-wide load_avg */
  load_avg = 0;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void)
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);

  /* Set current thread as the parent of spawned child */
  t->parent = thread_current ();
  tid = t->tid = allocate_tid ();

  /* Add new thread to child_list */
  list_push_back (&thread_current()->child_list, &t->child_elem);

  enum intr_level old_level = intr_disable();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;

  intr_set_level(old_level);

  /* Add to run queue and tests priorities. */
  thread_unblock (t);
  check_priority();

  return tid;
}

/* Puts the current thread to sleep until tick= time,
      updates alarmtime of thread. */
void
thread_sleep (int64_t time)
{

  struct thread *curr = thread_current();

  ASSERT(curr != idle_thread);
  curr->alarmtime = time;
  list_push_back(&sleep_list, &curr->sleep_elem);

  enum intr_level old_level = intr_disable();
  thread_block();

  intr_set_level (old_level);
}


/* Function called by timer_interrupt in timer.c if alarm rings,
        wakes up any thread that are on or past its alarmtime. */
int64_t
check_awake (int64_t time)
{
  enum intr_level old_level = intr_disable();
  int64_t new_alarm=INT_MAX;
  struct list_elem *e;
  for (e = list_begin (&sleep_list); e != list_end (&sleep_list); e = list_next(e))
  {
    struct thread *curr = list_entry (e, struct thread, sleep_elem);
    ASSERT (is_thread (curr));
    if (curr ==  idle_thread){
      printf("chcking blocked idle thread");
    }
    if(curr->alarmtime <= time)
    {
      list_remove(&curr->sleep_elem);
      thread_unblock(curr);
    }
    else if (curr->alarmtime < new_alarm && curr -> alarmtime > time)
      new_alarm = curr->alarmtime;
  }
  intr_set_level (old_level);
  return new_alarm;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void)
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t)
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if (t != idle_thread)
    list_insert_ordered (&ready_list, &t->elem, strict_priority, 0);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Removes threads from donor_list that were waitng for released lock. */
void
clear_lock (struct lock *lock)
{
  struct list_elem *e;
  struct list *donor_list = &thread_current()->donor_list;
  struct thread *thr;

  for (e = list_begin(donor_list); e != list_end(donor_list); e = list_next(e))
  {
    thr = list_entry(e, struct thread, donor_elem);
    if (thr -> waiting_lock == lock)
      list_remove(e);
  }
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void)
{
  struct thread *t = running_thread ();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void)
{
  ASSERT (!intr_context ());

  struct thread *curr= thread_current();

  /* If thread was exiting from a proper load, print exit message */
  if (curr->load_status!=-1)
    printf ("%s: exit(%d)\n", curr->name, curr->exit_status);

#ifdef USERPROG
  process_exit ();
#endif

  /* Just set our status to dying and schedule another process.
     We will be destroyed during the call to schedule_tail(). */
  intr_disable ();

  /* Tells parent process is exiting */
  sema_up (&curr->exit_lock);

  /* Waits for parent to release process */
  sema_down(&curr->delete_lock);

  curr->status = THREAD_DYING;

  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
  struct thread *curr = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (curr != idle_thread)
    list_insert_ordered (&ready_list, &curr->elem, strict_priority, 0);
  curr->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Binary function to compare priorities.
   Only returns true if priority first is strictly greater than second.
   In combination with list_insert_orderd:
    -does not break when priority is equal
    -breaks at first element with lower priority.
   Allows for thread with equal priority to be sent as far back as possible.
*/
bool
strict_priority (const struct list_elem *first,
                 const struct list_elem *second,
                 void *aux UNUSED)
{
  return (list_entry (first, struct thread, elem)->priority > list_entry (second, struct thread, elem)->priority);
}

/* Function called whenever priorities of threads have been changed,
      yields current thread if it is not highest priority. */
void
check_priority(void)
{
  enum intr_level old_level = intr_disable();
  if(!list_empty(&ready_list))
  {
    struct thread *front = list_entry(list_front(&ready_list), struct thread, elem);
    if (thread_get_priority() < front->priority)
    {
      intr_set_level(old_level);
      thread_yield();
    }
  }
  intr_set_level(old_level);
}


/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
  if(thread_mlfqs)
    return;

  int prev_priority = thread_get_priority ();
  thread_current ()->base_priority = new_priority;

  /* Updates priority based on new base_priority and list of donors */
  reset_priority ();

  /* Donates priority if priority increases */
  if (prev_priority < thread_get_priority ())
    donate_priority ();

  /* Checks for new highest priority if current priority decreases */
  if (prev_priority > thread_get_priority ())
    check_priority();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
  enum intr_level old_level = intr_disable();
  int tmp = thread_current ()->priority;
  intr_set_level(old_level);
  return tmp;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
  enum intr_level old_level = intr_disable();

  /* Force bounds on nice */
  if (nice > NICE_MAX)
    nice = NICE_MAX;
  else if (nice < NICE_MIN)
    nice = NICE_MIN;

  thread_current()->nice =  nice;
  mlfqs_check(thread_current());
  check_priority();

  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  enum intr_level old_level = intr_disable();
  int tmp = thread_current ()->nice;
  intr_set_level(old_level);
  return tmp;
}

/* Updates average load of past minute, callled once a second. */
void
update_load_avg (void)
{
  int ready_threads = list_size(&ready_list);
  if (thread_current()!=idle_thread)
    ready_threads++;
  load_avg = fp_div_int( (fp_mul_int(load_avg,59) + int_to_fp(ready_threads)), 60);
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
  enum intr_level old_level = intr_disable();
  int tmp = fp_to_int_round(fp_mul_int(load_avg,100));
  intr_set_level(old_level);
  return tmp;
}

/* Increment recent_cpu if cpu is utilized (non-idle thread is running) */
void
inc_recent_cpu (void)
{
  if (thread_current() != idle_thread)
    thread_current()->recent_cpu = fp_add_int(thread_current()->recent_cpu, 1);
}

/* MLFQS update for all threads (current, ready, and sleeping),
      calls thread_update_recent_cpu to update each threads recent_cpu value,
      then calls mlfqs_check to update mlfqs priority. */
void
update_recent_cpu (void)
{
  struct list_elem *e;
  struct thread *thr;

  /* Current thread */
  thread_update_recent_cpu(thread_current());
  mlfqs_check(thread_current());

  /* Ready threads */
  for(e = list_begin(&ready_list); e != list_end(&ready_list); e = list_next(e))
  {
    thr = list_entry(e, struct thread, elem);
    thread_update_recent_cpu(thr);
    mlfqs_check(thr);
  }

  /* Sleeping threads */
  for(e = list_begin(&sleep_list); e != list_end(&sleep_list); e = list_next(e))
  {
    thr = list_entry(e, struct thread, sleep_elem);
    thread_update_recent_cpu(thr);
    mlfqs_check(thr);
  }
}

/* Updates recent_cpu for a thread. */
void
thread_update_recent_cpu (struct thread *thr)
{
  if (thr == idle_thread)
      return;

  int tmp = fp_mul_int(load_avg, 2);
  thr->recent_cpu = fp_add_int(fp_mul(fp_div(tmp, fp_add_int(tmp, 1)), thr->recent_cpu), thr->nice);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
  enum intr_level old_level = intr_disable();
  int tmp = fp_to_int_round(fp_mul_int(thread_current()->recent_cpu,100));
  intr_set_level(old_level);
  return tmp;
}

/* Donates priority to holder of lock */
void
donate_priority ()
{
  struct thread *curr = thread_current();
  struct lock *l = curr->waiting_lock;

  /* Sets maximum length of donation chain (8 as suggested) */
  int chain = 0;
  while (chain <8)
  {
    /* No lock being waited on */
    if (!l)
      return;

    /* No one to donate to */
    if (!l->holder)
      return;

    if (l->holder->priority >= curr->priority)
    /* Holder of lock already has higher priority */
      return;

    l->holder->priority = curr->priority;

    /* Donee becomes new potential donor */
    curr = l->holder;
    l = curr->waiting_lock;
    chain++;
  }
}

/* Check list of donors to see what priority should be,
        this function simulates all of the donors redonating. */
void
reset_priority(void)
{
  struct list *donors =  &thread_current()->donor_list;
  thread_current()->priority = thread_current()->base_priority;

  struct thread *thr;
  struct list_elem *e;

  for (e = list_begin(donors); e != list_end(donors); e = list_next(e))
  {
    thr = list_entry (e, struct thread, donor_elem);
    if (thr->priority > thread_current()->priority)
      thread_current()->priority = thr->priority;
  }
}

/* Updates MLFQS priority of thread */
void
mlfqs_check(struct thread *thr)
{
  thr->priority = fp_to_int(int_to_fp(PRI_MAX) - fp_div_int(thr->recent_cpu, 4) - int_to_fp(thr->nice * 2));
  /* Force bounds on priority */
  if (thr->priority>PRI_MAX)
    thr->priority=PRI_MAX;
  if (thr->priority<PRI_MIN)
    thr->priority=PRI_MIN;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;)
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Since `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->base_priority = priority;

  /* Time for thread to be woken up */
  t->alarmtime = 0;

  /* Sets nice  */
  t->nice = 0;
  t->recent_cpu = 0;

  /* Initialize lock and donor relationships */
  t->waiting_lock =  NULL;
  list_init(&t->donor_list);

  t->magic = THREAD_MAGIC;

  /* Assignment 2 parent relationship */
  t->parent = NULL;
  list_init(&t->child_list);

  /* Assignment 2 load checks */
  t->load_status = 0;
  t->exit_status = 0;

  /* Process synchronization semaphores */
  sema_init (&t->load_lock, 0);
  sema_init (&t->exit_lock, 0);
  sema_init (&t->delete_lock, 0);

  /* Assignment 2 file descriptors */
  list_init(&t->file_list);
  t->fd = 2;              /* Skips stdin/stdout  */
  t->execfile = NULL;

  /* Supplementary page table */
  list_init(&t->spt);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void)
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
schedule_tail (struct thread *prev)
{
  struct thread *curr = running_thread ();

  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  curr->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != curr);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void)
{
  struct thread *curr = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (curr->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (curr != next)
    prev = switch_threads (curr, next);
  schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
