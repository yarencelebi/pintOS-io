#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <malloc.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "userprog/process.h"

struct lock filesys_lock;

/* ── Adres doğrulama ─────────────────────────────────────────── */

void
check_user_address (const void *addr)
{
  if (addr == NULL || !is_user_vaddr (addr))
    exit (-1);
}

/*
 * Sayfa sınırlarını kontrol ederek buffer'ın tamamının
 * user-space'de olduğunu doğrular. Her byte yerine her
 * sayfa başlangıcını kontrol etmek yeterli ve çok daha hızlı.
 */
void
check_user_buffer (const void *addr, unsigned size)
{
  if (size == 0)
    return;
  const char *p = (const char *) addr;
  check_user_address (p);
  check_user_address (p + size - 1);
  /* Aradaki sayfa sınırlarını da kontrol et */
  const char *page = (const char *) pg_round_down (p) + PGSIZE;
  for (; page < p + size; page += PGSIZE)
    check_user_address (page);
}

void
check_user_string (const char *str)
{
  check_user_address (str);
  while (*str != '\0')
    {
      str++;
      check_user_address (str);
    }
}

/* ── FD yönetimi ─────────────────────────────────────────────── */

struct file *
get_file_from_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->open_files);
       e != list_end (&t->open_files);
       e = list_next (e))
    {
      struct file_descriptor *fe =
          list_entry (e, struct file_descriptor, elem);
      if (fe->fd == fd)
        return fe->file;
    }
  return NULL;
}

int
add_file_to_thread (struct file *f)
{
  struct thread *t = thread_current ();
  struct file_descriptor *fe = malloc (sizeof (struct file_descriptor));
  if (fe == NULL)
    return -1;
  fe->file = f;
  fe->fd   = t->next_fd++;
  list_push_back (&t->open_files, &fe->elem);
  return fe->fd;
}

/* FD'yi listeden kaldırıp dosyayı kapatır. Bulunamazsa -1 döner. */
static int
close_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->open_files);
       e != list_end (&t->open_files);
       e = list_next (e))
    {
      struct file_descriptor *fe =
          list_entry (e, struct file_descriptor, elem);
      if (fe->fd == fd)
        {
          list_remove (&fe->elem);
          file_close (fe->file);
          free (fe);
          return 0;
        }
    }
  return -1;
}

/* ── Syscall handler ─────────────────────────────────────────── */

static void syscall_handler (struct intr_frame *f);

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  check_user_address (f->esp);
  int syscall_nr = *(int *) (f->esp);

  switch (syscall_nr)
    {
    /* ── HALT ──────────────────────────────────────────────── */
    case SYS_HALT:
      shutdown_power_off ();
      break;

    /* ── EXIT ──────────────────────────────────────────────── */
    case SYS_EXIT:
      {
        check_user_address ((int *) f->esp + 1);
        int status = *((int *) f->esp + 1);
        exit (status);
        break;
      }

    /* ── EXEC ──────────────────────────────────────────────── */
    case SYS_EXEC:
      {
        check_user_address ((int *) f->esp + 1);
        const char *cmd = *(const char **) ((int *) f->esp + 1);
        check_user_string (cmd);
        f->eax = (uint32_t) process_execute (cmd);
        break;
      }

    /* ── WAIT ──────────────────────────────────────────────── */
    case SYS_WAIT:
      {
        check_user_address ((int *) f->esp + 1);
        tid_t pid = *((int *) f->esp + 1);
        f->eax = (uint32_t) process_wait (pid);
        break;
      }

    /* ── CREATE ────────────────────────────────────────────── */
    case SYS_CREATE:
      {
        check_user_address ((int *) f->esp + 1);
        check_user_address ((int *) f->esp + 2);
        const char *name     = *(const char **) ((int *) f->esp + 1);
        unsigned initial_size = *((unsigned *) f->esp + 2);
        check_user_string (name);
        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) filesys_create (name, initial_size);
        lock_release (&filesys_lock);
        break;
      }

    /* ── REMOVE ────────────────────────────────────────────── */
    case SYS_REMOVE:
      {
        check_user_address ((int *) f->esp + 1);
        const char *name = *(const char **) ((int *) f->esp + 1);
        check_user_string (name);
        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) filesys_remove (name);
        lock_release (&filesys_lock);
        break;
      }

    /* ── OPEN ──────────────────────────────────────────────── */
    case SYS_OPEN:
      {
        check_user_address ((int *) f->esp + 1);
        const char *name = *(const char **) ((int *) f->esp + 1);
        check_user_string (name);
        lock_acquire (&filesys_lock);
        struct file *fp = filesys_open (name);
        lock_release (&filesys_lock);
        f->eax = (fp != NULL) ? (uint32_t) add_file_to_thread (fp)
                              : (uint32_t) -1;
        break;
      }

    /* ── FILESIZE ───────────────────────────────────────────── */
    case SYS_FILESIZE:
      {
        check_user_address ((int *) f->esp + 1);
        int fd = *((int *) f->esp + 1);
        struct file *fp = get_file_from_fd (fd);
        if (fp == NULL)
          { f->eax = (uint32_t) -1; break; }
        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) file_length (fp);
        lock_release (&filesys_lock);
        break;
      }

    /* ── READ ───────────────────────────────────────────────── */
    case SYS_READ:
      {
        check_user_address ((int *) f->esp + 1);
        check_user_address ((int *) f->esp + 2);
        check_user_address ((int *) f->esp + 3);
        int      fd   = *((int *)      f->esp + 1);
        char    *buf  = *(char **)    ((int *) f->esp + 2);
        unsigned size = *((unsigned *) f->esp + 3);
        check_user_buffer (buf, size);

        if (fd == 0)
          {
            for (unsigned i = 0; i < size; i++)
              buf[i] = input_getc ();
            f->eax = size;
          }
        else
          {
            struct file *fp = get_file_from_fd (fd);
            if (fp == NULL)
              { f->eax = (uint32_t) -1; break; }
            lock_acquire (&filesys_lock);
            f->eax = (uint32_t) file_read (fp, buf, size);
            lock_release (&filesys_lock);
          }
        break;
      }

    /* ── WRITE ──────────────────────────────────────────────── */
    case SYS_WRITE:
      {
        check_user_address ((int *) f->esp + 1);
        check_user_address ((int *) f->esp + 2);
        check_user_address ((int *) f->esp + 3);
        int      fd   = *((int *)      f->esp + 1);
        void    *buf  = *(void **)    ((int *) f->esp + 2);
        unsigned size = *((unsigned *) f->esp + 3);
        check_user_buffer (buf, size);

        if (fd == 1)
          {
            putbuf (buf, size);
            f->eax = size;
          }
        else
          {
            struct file *fp = get_file_from_fd (fd);
            if (fp == NULL)
              { f->eax = (uint32_t) -1; break; }
            lock_acquire (&filesys_lock);
            f->eax = (uint32_t) file_write (fp, buf, size);
            lock_release (&filesys_lock);
          }
        break;
      }

    /* ── SEEK ───────────────────────────────────────────────── */
    case SYS_SEEK:
      {
        check_user_address ((int *) f->esp + 1);
        check_user_address ((int *) f->esp + 2);
        int      fd       = *((int *)      f->esp + 1);
        unsigned position = *((unsigned *) f->esp + 2);
        struct file *fp = get_file_from_fd (fd);
        if (fp == NULL) break;
        lock_acquire (&filesys_lock);
        file_seek (fp, position);
        lock_release (&filesys_lock);
        break;
      }

    /* ── TELL ───────────────────────────────────────────────── */
    case SYS_TELL:
      {
        check_user_address ((int *) f->esp + 1);
        int fd = *((int *) f->esp + 1);
        struct file *fp = get_file_from_fd (fd);
        if (fp == NULL)
          { f->eax = (uint32_t) -1; break; }
        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) file_tell (fp);
        lock_release (&filesys_lock);
        break;
      }

    /* ── CLOSE ──────────────────────────────────────────────── */
    case SYS_CLOSE:
      {
        check_user_address ((int *) f->esp + 1);
        int fd = *((int *) f->esp + 1);
        if (fd < 2)           /* stdin/stdout kapat = geçersiz */
          exit (-1);
        close_fd (fd);
        break;
      }

    default:
      thread_exit ();
    }
}

/* ── exit() ─────────────────────────────────────────────────── */

/*
 * exit_status'u kaydeder ve thread_exit() çağırır.
 * Çıkış mesajını process_exit() basar — çift print yok.
 */
void
exit (int status)
{
  struct thread *t = thread_current ();
  t->exit_status = status;
  thread_exit ();
}
