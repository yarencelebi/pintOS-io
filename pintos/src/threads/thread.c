/* ========================= threads/thread.c ========================= */

#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "threads/fixed-point.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state. */
struct list ready_list;

/* List of all processes. */
static struct list all_list;

/* List of sleeping processes. */
static struct list sleep_list;

/* System load average (fixed-point). */
static int load_avg;

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
   If true, use multi-level feedback queue scheduler. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* MLFQS Helper Functions - ALL STATIC */
static void mlfqs_update_load_avg_and_recent_cpu (void);
static void mlfqs_update_all_priorities (void);
static void mlfqs_calc_priority (struct thread *t, void *aux UNUSED);
static void mlfqs_calc_recent_cpu (struct thread *t, void *aux UNUSED);

/* Priority comparison: larger priority comes first in list */
static bool
thread_priority_greater (const struct list_elem *a,
                         const struct list_elem *b,
                         void *aux UNUSED)
{
  return list_entry (a, struct thread, elem)->priority
       > list_entry (b, struct thread, elem)->priority;
}

/* Sleep list comparison function */
static bool
thread_wake_tick_less (const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED)
{
  return list_entry (a, struct thread, elem)->wake_tick
       < list_entry (b, struct thread, elem)->wake_tick;
}

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);
  load_avg = FP_FROM_INT (0);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

void
thread_start (void) 
{
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

void
thread_tick (void)
{
  struct thread *t = thread_current ();

  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* MLFQS recent_cpu update */
  if (thread_mlfqs && t != idle_thread)
    t->recent_cpu = FP_ADD_INT (t->recent_cpu, 1);

  int64_t current_ticks = idle_ticks + kernel_ticks + user_ticks;

  if (thread_mlfqs)
    {
      if (current_ticks % TIMER_FREQ == 0)
        {
          mlfqs_update_load_avg_and_recent_cpu ();
          mlfqs_update_all_priorities ();
        }
      else if (current_ticks % TIME_SLICE == 0)
        {
          mlfqs_update_all_priorities ();
        }
    }

  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

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

  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  thread_unblock (t);

  /* Preemption: if new thread has higher priority, yield CPU */
  if (thread_get_priority () < t->priority)
    {
      thread_yield ();
    }

  return tid;
}

void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

const char *
thread_name (void) 
{
  return thread_current ()->name;
}

struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);
  return t;
}

tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, thread_priority_greater, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;
  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Put thread to sleep until wake_tick */
void
thread_sleep (int64_t wake_tick)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  cur->wake_tick = wake_tick;
  list_insert_ordered (&sleep_list, &cur->elem, thread_wake_tick_less, NULL);
  thread_block ();
  intr_set_level (old_level);
}

/* Wake up sleeping threads whose time has come */
void
thread_wake (int64_t current_tick)
{
  struct list_elem *e = list_begin (&sleep_list);

  while (e != list_end (&sleep_list))
    {
      struct thread *t = list_entry (e, struct thread, elem);
      if (t->wake_tick > current_tick)
        break;
      e = list_remove (e);
      thread_unblock (t);
    }
}

/* ── PRIORITY DONATION SYSTEM ────────────────────────────── */

/* Get thread's effective priority considering donations */
int
thread_get_effective_priority (struct thread *t)
{
  int max_priority = t->base_priority;
  struct list_elem *e;

  for (e = list_begin (&t->donations); e != list_end (&t->donations); e = list_next (e))
    {
      struct thread *d = list_entry (e, struct thread, donation_elem);
      if (d->priority > max_priority)
        max_priority = d->priority;
    }
  return max_priority;
}

/* Perform cascading priority donation (max 8 levels deep) */
void
thread_donate_priority (void)
{
  int depth;
  struct thread *cur = thread_current ();
  struct thread *t = cur->waiting_lock ? cur->waiting_lock->holder : NULL;

  for (depth = 0; depth < 8 && t != NULL; depth++)
    {
      t->priority = thread_get_effective_priority (t);
      if (t->status == THREAD_READY)
        {
          list_remove (&t->elem);
          list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL);
        }
      t = t->waiting_lock ? t->waiting_lock->holder : NULL;
    }
}

/* Remove donations associated with a lock */
void
thread_remove_donation (struct lock *lock)
{
  struct thread *cur = thread_current ();
  struct list_elem *e = list_begin (&cur->donations);

  while (e != list_end (&cur->donations))
    {
      struct thread *t = list_entry (e, struct thread, donation_elem);
      if (t->waiting_lock == lock)
        e = list_remove (e);
      else
        e = list_next (e);
    }
}

/* Update thread priority and handle preemption */
void
thread_update_priority (void)
{
  struct thread *cur = thread_current ();
  cur->priority = thread_get_effective_priority (cur);

  if (!list_empty (&ready_list))
    {
      struct thread *front = list_entry (list_begin (&ready_list), struct thread, elem);
      if (front->priority > cur->priority)
        thread_yield ();
    }
}

void
thread_set_priority (int new_priority)
{
  if (thread_mlfqs)
    return;

  struct thread *cur = thread_current ();
  cur->base_priority = new_priority;
  thread_update_priority ();
}

int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* ── ADVANCED SCHEDULER (MLFQS) IMPLEMENTATION ───────────────── */

void
thread_set_nice (int new_nice)
{
  enum intr_level old_level = intr_disable ();
  struct thread *cur = thread_current ();
  cur->nice = new_nice;
  mlfqs_calc_priority (cur, NULL);
  thread_update_priority ();
  intr_set_level (old_level);
}

int
thread_get_nice (void)
{
  return thread_current ()->nice;
}

int
thread_get_load_avg (void)
{
  return FP_TO_INT_NEAR (FP_MUL_INT (load_avg, 100));
}

int
thread_get_recent_cpu (void)
{
  return FP_TO_INT_NEAR (FP_MUL_INT (thread_current ()->recent_cpu, 100));
}

static void
mlfqs_calc_priority (struct thread *t, void *aux UNUSED)
{
  if (t == idle_thread)
    return;
  int p = PRI_MAX - FP_TO_INT_NEAR (FP_DIV_INT (t->recent_cpu, 4)) - t->nice * 2;
  if (p < PRI_MIN) p = PRI_MIN;
  if (p > PRI_MAX) p = PRI_MAX;
  t->priority = p;
}

static void
mlfqs_calc_recent_cpu (struct thread *t, void *aux UNUSED)
{
  if (t == idle_thread)
    return;
  int twice_load = FP_MUL_INT (load_avg, 2);
  int coeff = FP_DIV (twice_load, FP_ADD_INT (twice_load, 1));
  t->recent_cpu = FP_ADD_INT (FP_MUL (coeff, t->recent_cpu), t->nice);
}

static void
mlfqs_update_load_avg_and_recent_cpu (void)
{
  size_t ready_threads = list_size (&ready_list);
  if (thread_current () != idle_thread)
    ready_threads++;

  load_avg = FP_ADD (FP_MUL (FP_DIV (FP_FROM_INT (59), FP_FROM_INT (60)), load_avg),
                     FP_MUL_INT (FP_DIV (FP_FROM_INT (1), FP_FROM_INT (60)), ready_threads));
  if (load_avg < 0)
    load_avg = 0;

  thread_foreach (mlfqs_calc_recent_cpu, NULL);
}

static void
mlfqs_update_all_priorities (void)
{
  thread_foreach (mlfqs_calc_priority, NULL);
}

/* ────────────────────────────────────────────────────────────── */

static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      intr_disable ();
      thread_block ();
      asm volatile ("sti; hlt" : : : "memory");
    }
}

static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();
  function (aux);
  thread_exit ();
}

struct thread *
running_thread (void) 
{
  uint32_t *esp;
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->base_priority = priority;
  t->waiting_lock = NULL;
  t->nice = 0;
  t->recent_cpu = FP_FROM_INT (0);
  list_init (&t->donations);
  t->magic = THREAD_MAGIC;
  
#ifdef USERPROG
  t->exit_status = -1;
  sema_init (&t->load_sema, 0);
  t->load_success = false;
  t->parent = NULL;
  list_init (&t->open_files);
  t->next_fd = 2;
  t->executable_file = NULL;
  list_init (&t->child_list);
  t->waited = false;
  sema_init (&t->wait_sema, 0);
  sema_init (&t->die_sema, 0);
#endif

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);
  cur->status = THREAD_RUNNING;
  thread_ticks = 0;

#ifdef USERPROG
  process_activate ();
#endif

  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

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

uint32_t thread_stack_ofs = offsetof (struct thread, stack);
