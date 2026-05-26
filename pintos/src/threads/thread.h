#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "threads/fixed-point.h"
/* Liste tanımlamaları - Diğer dosyalardan erişebilmek için */
extern struct list all_threads;
extern struct list ready_list;

/* Thread states. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Ready to run. */
    THREAD_BLOCKED,     /* Blocked. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   An integer type that can be accommodating any given thread ID. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Thread foreach function prototype */
typedef void thread_action_func (struct thread *t, void *aux);

/* Child process status tracking structure */
struct child_status
  {
    tid_t tid;
    int exit_status;
    bool exited;
    struct list_elem elem;
  };

/* File descriptor structure */
struct file_descriptor
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

/* A network kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Hence, the minimum stack size is
   almost 4 kB; a huge stack allocation will overflow the
   page and corrupt the thread structure. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Current effective priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    /* ── PROJECT 1: THREADS (Alarm Clock & Priority Donation) ── */
    int64_t wake_tick;                  /* Alarm clock wake time */
    int base_priority;                  /* Original priority value */
    struct lock *waiting_lock;          /* Lock being waited for */
    struct list donations;              /* Priority donations to this thread */
    struct list_elem donation_elem;     /* Donation list element */

    /* ── PROJECT 1: ADVANCED SCHEDULER (MLFQS) ── */
    int nice;                           /* Nice value */
    int recent_cpu;                     /* Recent CPU value (fixed-point) */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

    /* ── PROJECT 2: USERPROG (User Programs) ── */
    int exit_status;                    /* Process exit code */
    struct semaphore load_sema;         /* Semaphore for load synchronization */
    bool load_success;                  /* Whether load succeeded */
    struct thread *parent;              /* Parent thread pointer */
    struct list open_files;             /* List of open files */
    int next_fd;                        /* Next file descriptor to assign */
    struct file *executable_file;       /* Executable file (write-protected) */
    struct list child_list;             /* List of child processes */
    struct list_elem child_elem;        /* Element in parent's child list */
    bool waited;                        /* Whether parent has waited on this */
    struct semaphore wait_sema;         /* Semaphore for process_wait */
    struct semaphore die_sema;          /* Semaphore for safe cleanup */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* Exported for synch.c - ready list and comparison function */
extern struct list ready_list;
bool thread_priority_greater (const struct list_elem *a,
                              const struct list_elem *b,
                              void *aux);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* Project 1 helper function prototypes */
void thread_sleep (int64_t wake_tick);
void thread_wake (int64_t current_tick);
void thread_donate_priority (void);
void thread_remove_donation (struct lock *lock);
int thread_get_effective_priority (struct thread *t);
void thread_update_priority (void);

#endif /* threads/thread.h */
