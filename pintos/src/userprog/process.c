#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool push_arguments (void **esp, const char *cmdline);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  struct thread *cur = thread_current ();

  /* Allocate kernel page for file_name */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Parse program name from command line */
  char *name_copy = palloc_get_page (0);
  if (name_copy == NULL)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  strlcpy (name_copy, file_name, PGSIZE);
  char *save_ptr;
  char *prog_name = strtok_r (name_copy, " ", &save_ptr);

  /* Create new child thread */
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy);

  palloc_free_page (name_copy);

  if (tid == TID_ERROR)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }

  /* Set parent pointer for child */
  struct thread *child = thread_from_tid (tid);
  if (child != NULL)
    {
      child->parent = cur;
    }

  /* Wait for child process to load */
  sema_down (&cur->load_sema);

  /* Return error if load failed */
  if (!cur->load_success)
    return TID_ERROR;

  return tid;
}

/* Find thread by tid - uses all_list */
struct thread *
thread_from_tid (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if (t->tid == tid)
        return t;
    }
  return NULL;
}

/* A thread function that loads a user process and starts it running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread *cur = thread_current ();

  /* Initialize interrupt frame */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  /* Load program and set up stack */
  success = load (file_name, &if_.eip, &if_.esp);

  /* Notify parent of load status */
  if (cur->parent != NULL)
    {
      cur->parent->load_success = success;
      sema_up (&cur->parent->load_sema);
    }

  palloc_free_page (file_name);

  /* Exit if load failed */
  if (!success)
    thread_exit ();

  /* Switch to user mode */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for a child process to die and returns its exit status. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct thread *child = NULL;

  /* Search for child in all threads */
  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if (t->tid == child_tid && t->parent == cur)
        {
          child = t;
          break;
        }
    }

  /* Return -1 if child not found or already waited */
  if (child == NULL || child->waited)
    return -1;

  child->waited = true;

  /* Block until child exits */
  sema_down (&child->wait_sema);

  int status = child->exit_status;

  /* Allow child to be cleaned up */
  sema_up (&child->die_sema);

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Close all open files */
  struct list_elem *e;
  while (!list_empty (&cur->open_files))
    {
      e = list_pop_front (&cur->open_files);
      struct file_descriptor *fd_entry = list_entry (e, struct file_descriptor, elem);
      file_close (fd_entry->file);
      free (fd_entry);
    }

  /* Remove write protection from executable */
  if (cur->executable_file != NULL)
    {
      file_allow_write (cur->executable_file);
      file_close (cur->executable_file);
      cur->executable_file = NULL;
    }

  /* Wake up parent if it's waiting */
  sema_up (&cur->wait_sema);

  /* Wait for parent to read exit status */
  if (cur->parent != NULL)
    sema_down (&cur->die_sema);

  /* Destroy page directory */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current thread. */
void
process_activate (void)
{
  struct thread *t = thread_current ();
  pagedir_activate (t->pagedir);
  tss_update ();
}

/* ---- ELF types ---- */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32
#define PE32Ax PRIx32
#define PE32Ox PRIx32
#define PE32Hx PRIx16

struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_STACK   0x6474e551

#define PF_X 1
#define PF_W 2
#define PF_R 4

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* ELF binary loader main function */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Get temporary copy for parsing command line */
  char *fn_copy = palloc_get_page (0);
  if (fn_copy == NULL) 
    goto done;
  strlcpy (fn_copy, file_name, PGSIZE);

  char *save_ptr;
  char *prog_name = strtok_r (fn_copy, " ", &save_ptr);

  /* Create page directory */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open file */
  lock_acquire (&filesys_lock);
  file = filesys_open (prog_name);
  lock_release (&filesys_lock);
  
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", prog_name);
      goto done; 
    }

  /* Deny writes to executable file */
  file_deny_write (file);
  t->executable_file = file;

  /* Verify ELF header */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", prog_name);
      goto done; 
    }

  /* Read and load segments */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Setup user stack */
  if (!setup_stack (esp))
    goto done;

  /* Push command line arguments */
  if (!push_arguments (esp, file_name))
    goto done;

  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

 done:
  palloc_free_page (fn_copy);
  /* File will be closed in process_exit or on failure */
  if (!success && file != NULL)
    {
      file_allow_write (file);
      file_close (file);
    }
  return success;
}

static bool install_page (void *upage, void *kpage, bool writable);

static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 
  if (phdr->p_memsz == 0)
    return false;
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;
  if (phdr->p_vaddr < PGSIZE)
    return false;
  return true;
}

static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE; 
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Push command line arguments onto stack in x86 format */
static bool
push_arguments (void **esp, const char *cmdline)
{
  char *cmd_copy = palloc_get_page (0);
  if (cmd_copy == NULL)
    return false;
  strlcpy (cmd_copy, cmdline, PGSIZE);

  char *argv[128];
  int argc = 0;
  char *token, *save_ptr;

  for (token = strtok_r (cmd_copy, " ", &save_ptr);
       token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    {
      argv[argc++] = token;
      if (argc >= 128)
        break;
    }

  char *arg_ptrs[128];
  int i;
  for (i = argc - 1; i >= 0; i--)
    {
      size_t len = strlen (argv[i]) + 1;
      *esp -= len;
      memcpy (*esp, argv[i], len);
      arg_ptrs[i] = *esp;
    }

  /* Align stack to 4-byte boundary */
  *esp = (void *) ((uintptr_t)*esp & ~3);

  /* Push NULL sentinel for argv array */
  *esp -= sizeof (char *);
  *(char **)*esp = NULL;

  /* Push argv pointers in reverse order */
  for (i = argc - 1; i >= 0; i--)
    {
      *esp -= sizeof (char *);
      *(char **)*esp = arg_ptrs[i];
    }

  /* Push argv (pointer to argv array) */
  char **argv_on_stack = (char **)*esp;
  *esp -= sizeof (char **);
  *(char ***)*esp = argv_on_stack;

  /* Push argc */
  *esp -= sizeof (int);
  *(int *)*esp = argc;

  /* Push return address (NULL) */
  *esp -= sizeof (void *);
  *(void **)*esp = NULL;

  palloc_free_page (cmd_copy);
  return true;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
