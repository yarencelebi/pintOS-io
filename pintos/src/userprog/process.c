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
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);


tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  struct thread *cur = thread_current ();

  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Sadece program adını thread ismi olarak geç (argümanlar hariç) */
  char name_copy[16];
  strlcpy (name_copy, file_name, sizeof name_copy);
  char *save_ptr;
  char *prog_name = strtok_r (name_copy, " ", &save_ptr);

  tid = thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy);

  if (tid == TID_ERROR)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }


  sema_down (&cur->load_sema);

  if (!cur->load_success)
    return TID_ERROR;

  return tid;
}

static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread *cur = thread_current ();

  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);


  if (cur->parent != NULL)
    {
      cur->parent->load_success = success;
      sema_up (&cur->parent->load_sema);
    }

  palloc_free_page (file_name);

  if (!success)
    thread_exit ();

  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}



int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct thread *child = NULL;

  for (e = list_begin (&cur->children);
       e != list_end (&cur->children);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, child_elem);
      if (t->tid == child_tid)
        {
          child = t;
          break;
        }
    }

  /* Bulunamadı veya zaten wait edildi */
  if (child == NULL || child->waited)
    return -1;

  child->waited = true;

  /* Child exit olana kadar bekle */
  sema_down (&child->wait_sema);

  int status = child->exit_status;

  /* Child struct'ının serbest kalmasına izin ver */
  list_remove (&child->child_elem);
  sema_up (&child->die_sema);

  return status;
}


void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  printf ("%s: exit(%d)\n", cur->name, cur->exit_status);

  /* Açık dosyaları kapat */
  struct list_elem *e;
  while (!list_empty (&cur->open_files))
    {
      e = list_pop_front (&cur->open_files);
      struct file_descriptor *fd_entry =
          list_entry (e, struct file_descriptor, elem);
      file_close (fd_entry->file);
      free (fd_entry);
    }

  /*
   * Parent'ı uyandır. Eğer parent yoksa veya parent zaten ölmüşse
   * (wait_sema'yı kimse beklemiyor) die_sema'yı beklemeden geç.
   */
  sema_up (&cur->wait_sema);

  if (cur->parent != NULL)
    sema_down (&cur->die_sema);   /* Parent list_remove + sema_up yapana kadar bekle */

  pd = cur->pagedir;
  if (pd != NULL) 
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

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

static bool setup_stack (void **esp, char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /*
   * FIX: Argümanları parse et. file_name "prog arg1 arg2" formatında
   * gelebilir; sadece ilk token'ı dosya adı olarak kullan.
   */
  char fn_copy[256];
  strlcpy (fn_copy, file_name, sizeof fn_copy);
  char *save_ptr;
  char *prog_name = strtok_r (fn_copy, " ", &save_ptr);

  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  file = filesys_open (prog_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", prog_name);
      goto done; 
    }

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


  if (!setup_stack (esp, (char *) file_name))
    goto done;

  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

 done:
  file_close (file);
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
setup_stack (void **esp, char *file_name)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage == NULL)
    return false;

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (!success)
    {
      palloc_free_page (kpage);
      return false;
    }

  *esp = PHYS_BASE;

  /* --- Argümanları parse et ve stack'e yerleştir --- */

  /* Argüman string ve pointer'larını tutmak için geçici diziler */
  char *argv[128];
  int argc = 0;

  /* file_name'i kopyala (strtok_r bozuyor) */
  char fn_buf[256];
  strlcpy (fn_buf, file_name, sizeof fn_buf);

  char *token, *save_ptr;
  for (token = strtok_r (fn_buf, " ", &save_ptr);
       token != NULL && argc < 127;
       token = strtok_r (NULL, " ", &save_ptr))
    {
      argv[argc++] = token;
    }

  /* 1) Stringleri stack'e push et (PHYS_BASE'den aşağı doğru) */
  char *arg_ptrs[128];  /* stack üzerindeki adresleri tut */
  int i;
  for (i = argc - 1; i >= 0; i--)
    {
      size_t len = strlen (argv[i]) + 1;  /* null terminator dahil */
      *esp -= len;
      memcpy (*esp, argv[i], len);
      arg_ptrs[i] = (char *) *esp;
    }

  /* 2) Word-align: esp'yi 4'ün katına hizala */
  uintptr_t esp_val = (uintptr_t) *esp;
  esp_val &= ~(uintptr_t) 3;
  *esp = (void *) esp_val;

  /* 3) NULL sentinel: argv[argc] = NULL */
  *esp -= sizeof (char *);
  *(char **) *esp = NULL;

  /* 4) argv pointer'larını ters sırada push et (argv[argc-1] → argv[0]) */
  for (i = argc - 1; i >= 0; i--)
    {
      *esp -= sizeof (char *);
      *(char **) *esp = arg_ptrs[i];
    }

  /* 5) argv'nin kendisini push et (argv[0]'ın adresi) */
  char **argv_ptr = (char **) *esp;
  *esp -= sizeof (char **);
  *(char ***) *esp = argv_ptr;

  /* 6) argc'yi push et */
  *esp -= sizeof (int);
  *(int *) *esp = argc;

  /* 7) Sahte return address push et */
  *esp -= sizeof (void *);
  *(void **) *esp = NULL;

  return true;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
