/* ========================= threads/thread.h ========================= */

#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

struct lock;
struct file;

#ifdef USERPROG
/* Kullanıcı programlarının açtığı dosyaları takip etmek için yardımcı yapı */
struct file_descriptor
  {
    int fd;                         /* File descriptor id */
    struct file *file;              /* Açık olan dosyanın pointer'ı */
    struct list_elem elem;          /* thread->open_files listesi için eleman */
  };

/* Parent-Child ilişkisini çocuk ölse bile korumak için gerekli yapı */
struct child_status
  {
    tid_t tid;                      /* Çocuğun thread ID'si */
    int exit_status;                /* Çocuğun çıkış kodu (exit code) */
    bool has_exited;                /* Çocuk süreç sonlandı mı? */
    bool was_waited;                /* Parent bu çocuk için wait çağırdı mı? */
    struct semaphore wait_sema;     /* Parent'ı wait syscall'unda bloklamak için */
    struct list_elem elem;          /* thread->children listesi için eleman */
  };
#endif

struct thread
  {
    /* Pintos'un kendi orijinal değişkenleri */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    
    /* ── PROJECT 2 USERPROG İÇİN EKLENEN DEĞİŞKENLER ── */
    int exit_status;                    /* Sürecin çıkış kodu */
    struct semaphore exit_sema;         /* Parent'ın beklemesi için semaphore */
    struct list open_files;             /* Açık dosyaların listesi */
    int next_fd;                        /* Verilecek bir sonraki dosya deskriptörü (FD) */
    struct file *executable;            /* Çalıştırılan binary dosya (yazma koruması için) */
    struct child_status *status_in_parent; /* Parent'ın listesindeki durum referansı */
    struct list child_list;             /* Çocuk süreçlerin listesi */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* Global değişkenler */
extern bool thread_mlfqs;
extern struct list ready_list;
extern uint32_t thread_stack_ofs;

/* --- Fonksiyon Protokolleri --- */

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

/* Alarmlar ve Öncelik Yönetimi */
void thread_sleep (int64_t wake_tick);
void thread_wake (int64_t current_tick);
bool thread_priority_greater (const struct list_elem *a, const struct list_elem *b, void *aux);

/* Priority Donation fonksiyon tanımlamaları */
int thread_get_effective_priority (struct thread *t);
void thread_donate_priority (void);
void thread_remove_donation (struct lock *lock);
void thread_update_priority (void);

/* Getter / Setter arabirimleri */
int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
