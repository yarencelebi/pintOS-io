#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

#include "threads/synch.h"
#include "lib/kernel/list.h"

enum thread_status
  {
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_BLOCKED,
    THREAD_DYING
  };

typedef int tid_t;
#define TID_ERROR ((tid_t) -1)

#define PRI_MIN 0
#define PRI_DEFAULT 31
#define PRI_MAX 63

struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;
    enum thread_status status;
    char name[16];
    uint8_t *stack;
    int priority;
    struct list_elem allelem;

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;

#ifdef USERPROG
    /* Açık dosya yönetimi */
    struct list open_files;
    int next_fd;
    uint32_t *pagedir;

    /* Süreç yönetimi */
    struct thread *parent;       
    struct list children;
    struct list_elem child_elem;

    /* wait_sema: child exit olunca parent'ı uyandırır (0→1) */
    struct semaphore wait_sema;
    /* die_sema: parent wait bitince child'ın struct'ını serbest bırakır (0→1) */
    struct semaphore die_sema;
    /* load_sema: child load bitince parent'ı uyandırır (0→1) */
    struct semaphore load_sema;

    int exit_status;
    bool load_success;
    bool waited;                  
#endif

    /* Owned by thread.c. */
    unsigned magic;
  };

extern bool thread_mlfqs;

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

/* process.c tarafından kullanılan fonksiyon bildirimi */
struct thread *get_thread_by_tid (tid_t tid);

typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
