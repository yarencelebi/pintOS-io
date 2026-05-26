#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *f);

/* ── Address Validation (Security) ──────────────────────────────── */

void
check_user_address (const void *addr)
{
  struct thread *cur = thread_current ();
  if (addr == NULL || !is_user_vaddr (addr) || pagedir_get_page (cur->pagedir, addr) == NULL)
    {
      exit (-1);
    }
}

void
check_user_buffer (const void *addr, unsigned size)
{
  if (size == 0)
    return;

  const char *p = (const char *) addr;
  check_user_address (p);
  check_user_address (p + size - 1);

  /* Check all page boundaries */
  const char *page = (const char *) pg_round_down (p) + PGSIZE;
  for (; page < p + size; page += PGSIZE)
    {
      check_user_address (page);
    }
}

void
check_user_string (const char *str)
{
  check_user_address (str);
  while (true)
    {
      check_user_address (str);
      if (*str == '\0')
        break;
      str++;
    }
}

/* ── File Descriptor Management ────────────────────────────────── */

struct file *
get_file_from_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->open_files);
       e != list_end (&t->open_files);
       e = list_next (e))
    {
      struct file_descriptor *fe = list_entry (e, struct file_descriptor, elem);
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
  fe->fd = t->next_fd++;
  list_push_back (&t->open_files, &fe->elem);
  return fe->fd;
}

static int
close_fd (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->open_files);
       e != list_end (&t->open_files);
       e = list_next (e))
    {
      struct file_descriptor *fe = list_entry (e, struct file_descriptor, elem);
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

/* ── Syscall Initialization ────────────────────────────────────── */

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* ── Syscall Handler ───────────────────────────────────────────── */

static void
syscall_handler (struct intr_frame *f)
{
  check_user_address (f->esp);

  int syscall_nr = *(int *) f->esp;

  switch (syscall_nr)
    {
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
      {
        check_user_address (f->esp + 4);
        int status = *(int *) (f->esp + 4);
        exit (status);
        break;
      }

    case SYS_EXEC:
      {
        check_user_address (f->esp + 4);
        const char *cmd = *(const char **) (f->esp + 4);
        check_user_string (cmd);
        f->eax = (uint32_t) process_execute (cmd);
        break;
      }

    case SYS_WAIT:
      {
        check_user_address (f->esp + 4);
        tid_t pid = *(tid_t *) (f->esp + 4);
        f->eax = (uint32_t) process_wait (pid);
        break;
      }

    case SYS_CREATE:
      {
        check_user_address (f->esp + 4);
        check_user_address (f->esp + 8);
        const char *name = *(const char **) (f->esp + 4);
        unsigned initial_size = *(unsigned *) (f->esp + 8);
        check_user_string (name);

        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) filesys_create (name, initial_size);
        lock_release (&filesys_lock);
        break;
      }

    case SYS_REMOVE:
      {
        check_user_address (f->esp + 4);
        const char *name = *(const char **) (f->esp + 4);
        check_user_string (name);

        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) filesys_remove (name);
        lock_release (&filesys_lock);
        break;
      }

    case SYS_OPEN:
      {
        check_user_address (f->esp + 4);
        const char *name = *(const char **) (f->esp + 4);
        check_user_string (name);

        lock_acquire (&filesys_lock);
        struct file *fp = filesys_open (name);
        lock_release (&filesys_lock);

        if (fp != NULL)
          f->eax = (uint32_t) add_file_to_thread (fp);
        else
          f->eax = (uint32_t) -1;
        break;
      }

    case SYS_FILESIZE:
      {
        check_user_address (f->esp + 4);
        int fd = *(int *) (f->esp + 4);

        struct file *fp = get_file_from_fd (fd);
        if (fp == NULL)
          {
            f->eax = (uint32_t) -1;
            break;
          }

        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) file_length (fp);
        lock_release (&filesys_lock);
        break;
      }

    case SYS_READ:
      {
        check_user_address (f->esp + 4);
        check_user_address (f->esp + 8);
        check_user_address (f->esp + 12);

        int fd = *(int *) (f->esp + 4);
        char *buf = *(char **) (f->esp + 8);
        unsigned size = *(unsigned *) (f->esp + 12);
        check_user_buffer (buf, size);

        if (fd == 0)  /* STDIN */
          {
            unsigned i;
            for (i = 0; i < size; i++)
              buf[i] = input_getc ();
            f->eax = size;
          }
        else
          {
            struct file *fp = get_file_from_fd (fd);
            if (fp == NULL)
              {
                f->eax = (uint32_t) -1;
                break;
              }
            lock_acquire (&filesys_lock);
            f->eax = (uint32_t) file_read (fp, buf, size);
            lock_release (&filesys_lock);
          }
        break;
      }

    case SYS_WRITE:
      {
        check_user_address (f->esp + 4);
        check_user_address (f->esp + 8);
        check_user_address (f->esp + 12);

        int fd = *(int *) (f->esp + 4);
        const void *buf = *(const void **) (f->esp + 8);
        unsigned size = *(unsigned *) (f->esp + 12);
        check_user_buffer (buf, size);

        if (fd == 1)  /* STDOUT */
          {
            putbuf (buf, size);
            f->eax = size;
          }
        else
          {
            struct file *fp = get_file_from_fd (fd);
            if (fp == NULL)
              {
                f->eax = (uint32_t) -1;
                break;
              }
            lock_acquire (&filesys_lock);
            f->eax = (uint32_t) file_write (fp, buf, size);
            lock_release (&filesys_lock);
          }
        break;
      }

    case SYS_SEEK:
      {
        check_user_address (f->esp + 4);
        check_user_address (f->esp + 8);

        int fd = *(int *) (f->esp + 4);
        unsigned position = *(unsigned *) (f->esp + 8);

        struct file *fp = get_file_from_fd (fd);
        if (fp != NULL)
          {
            lock_acquire (&filesys_lock);
            file_seek (fp, position);
            lock_release (&filesys_lock);
          }
        break;
      }

    case SYS_TELL:
      {
        check_user_address (f->esp + 4);
        int fd = *(int *) (f->esp + 4);

        struct file *fp = get_file_from_fd (fd);
        if (fp == NULL)
          {
            f->eax = (uint32_t) -1;
            break;
          }

        lock_acquire (&filesys_lock);
        f->eax = (uint32_t) file_tell (fp);
        lock_release (&filesys_lock);
        break;
      }

    case SYS_CLOSE:
      {
        check_user_address (f->esp + 4);
        int fd = *(int *) (f->esp + 4);

        if (fd < 2)
          {
            exit (-1);
          }
        close_fd (fd);
        break;
      }

    default:
      exit (-1);
    }
}

/* ── Exit Helper ────────────────────────────────────────────────– */

void
exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  
  printf ("%s: exit(%d)\n", cur->name, status);

  thread_exit ();
}
