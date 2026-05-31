#include "userprog/syscall.h"
#include "pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* Gerekli ek kütüphanelerimizi dahil ediyoruz */
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/filesys.h"  // Dosya işlemleri için
#include "filesys/file.h"     // Dosya yapıları için
#include "threads/synch.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void check_valid_ptr (const void *vaddr);

struct lock filesys_lock;

void
syscall_init (void) 
{
lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* ADM 3: Kullanıcıdan gelen bellek adreslerini doğrulayan güvenlik fonksiyonu */
/* Güvenlik fonksiyonunu güncelliyoruz */
static void
check_valid_ptr (const void *vaddr)
{
  if (vaddr == NULL || !is_user_vaddr (vaddr)||pagedir_get_page (thread_current ()->pagedir, vaddr) == NULL)
    {
      /* Artık printf yok. Sadece durumu -1 yap ve çık. process_exit mesajı basacak. */
      thread_current ()->exit_status = -1;
      thread_exit ();
    }
}

/* ADIM 4: Sistem Çağrısı Seçici Altyapısı (Handler) */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Kullanıcı yığıtının (stack pointer) geçerli bir adreste olduğunu kontrol et */
  check_valid_ptr (f->esp);

  /* Yığıtın en tepesinden sistem çağrısının numarasını oku */
  int syscall_num = *(int *)f->esp;

  switch (syscall_num)
    {
	
	case SYS_OPEN:
      check_valid_ptr (f->esp + 4);
      const char *open_file = *(char **)(f->esp + 4);
      check_valid_ptr (open_file);

      lock_acquire (&filesys_lock);
      struct file *opened_file = filesys_open (open_file);
      lock_release (&filesys_lock);

      if (opened_file == NULL)
        {
          f->eax = -1; /* Dosya bulunamadı veya açılamadı */
        }
      else
        {
          struct thread *cur = thread_current ();
          if (cur->next_fd < 128)
            {
              cur->fd_table[cur->next_fd] = opened_file;
              f->eax = cur->next_fd; /* Programcıya dosya numarasını döndür */
              cur->next_fd++;
            }
          else
            {
              f->eax = -1; /* Tablo dolu */
            }
        }
      break;

    case SYS_CLOSE:
      check_valid_ptr (f->esp + 4);
      int close_fd = *(int *)(f->esp + 4);

      if (close_fd >= 2 && close_fd < 128)
        {
          struct thread *cur = thread_current ();
          if (cur->fd_table[close_fd] != NULL)
            {
              lock_acquire (&filesys_lock);
              file_close (cur->fd_table[close_fd]);
              lock_release (&filesys_lock);
              cur->fd_table[close_fd] = NULL; /* Alanı boşalt */
            }
        }
      break;		


	case SYS_EXEC:
      /* exec(cmd_line) için yığıttan komut satırı adresini oku */
      check_valid_ptr (f->esp + 4);
      const char *cmd_line = *(char **)(f->esp + 4);
      check_valid_ptr (cmd_line);

      /* Yeni süreci başlat ve thread/süreç id'sini geri döndür */
      f->eax = process_execute (cmd_line);
      break;


	case SYS_WAIT:
      /* wait(pid) için yığıttan çocuk sürecin id'sini oku */
      check_valid_ptr (f->esp + 4);
      int pid = *(int *)(f->esp + 4);

      /* Çocuk sürecin bitmesini bekle ve çıkış durumunu döndür */
      f->eax = process_wait (pid);
      break;

	case SYS_CREATE:
      /* create(file, initial_size) için argümanları oku */
      check_valid_ptr (f->esp + 4);
      check_valid_ptr (f->esp + 8);
      const char *create_file = *(char **)(f->esp + 4);
      unsigned initial_size = *(unsigned *)(f->esp + 8);
      check_valid_ptr (create_file);

      /* Pintos dosya sistemi thread-safe olmadığı için kilitleyip işlem yapıyoruz */
      lock_acquire (&filesys_lock);
      f->eax = filesys_create (create_file, initial_size);
      lock_release (&filesys_lock);
      break;

    case SYS_REMOVE:
      /* remove(file) için dosya adını oku */
      check_valid_ptr (f->esp + 4);
      const char *remove_file = *(char **)(f->esp + 4);
      check_valid_ptr (remove_file);

      lock_acquire (&filesys_lock);
      f->eax = filesys_remove (remove_file);
      lock_release (&filesys_lock);
      break;
	
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
  check_valid_ptr (f->esp + 4);
    int status = *(int *)(f->esp + 4);
    thread_current ()->exit_status = status;
printf ("%s: exit(%d)\n", thread_current ()->name, status); //testlerin dogru okuyabilmesi için

  /* <-- BU SATIR EKSİKTİ */
    thread_exit ();
	break;


    case SYS_WRITE:
      {
        int fd = *(int *)(f->esp + 4);
        const void *buffer = *(char **)(f->esp + 8);
        unsigned size = *(unsigned *)(f->esp + 12);

        check_valid_ptr(f->esp + 4);
        check_valid_ptr(f->esp + 8);
        check_valid_ptr(f->esp + 12);
        check_valid_ptr(buffer);

        if (fd == 1) {
            putbuf(buffer, size);
            f->eax = size;
        } 
        else if (fd >= 2) { // Dosyaya yazma durumu
            struct thread *t = thread_current();
            // Burada FD tablonu kontrol etmen lazım
            if (t->fd_table[fd] != NULL) {
                lock_acquire(&filesys_lock); // KİLİT ŞART
                f->eax = file_write(t->fd_table[fd], buffer, size);
                lock_release(&filesys_lock);
            } else {
                f->eax = -1;
            }
        } else {
            f->eax = -1;
        }
        break;
      }
    default:
      /* Bilinmeyen bir çağrı gelirse sistemi çökertmemek için süreci kapat */
      printf ("%s: exit(-1)\n", thread_current ()->name);
      thread_exit ();
      break;
    }
}
