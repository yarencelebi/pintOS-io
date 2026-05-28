#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* Gerekli ek kütüphanelerimizi dahil ediyoruz */
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void check_valid_ptr (const void *vaddr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* ADM 3: Kullanıcıdan gelen bellek adreslerini doğrulayan güvenlik fonksiyonu */
/* Güvenlik fonksiyonunu güncelliyoruz */
static void
check_valid_ptr (const void *vaddr)
{
  if (vaddr == NULL || !is_user_vaddr (vaddr))
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
    case SYS_HALT:
      shutdown_power_off ();
      break;

    case SYS_EXIT:
  check_valid_ptr (f->esp + 4);
  {
    int status = *(int *)(f->esp + 4);
    thread_current ()->exit_status = status;   /* <-- BU SATIR EKSİKTİ */
    thread_exit ();
  }
  break;

    case SYS_WRITE:
      /* write(fd, buffer, size) için yığıttan argümanları sırayla oku */
      check_valid_ptr (f->esp + 4);  /* fd */
      check_valid_ptr (f->esp + 8);  /* buffer pointer */
      check_valid_ptr (f->esp + 12); /* size */

      int fd = *(int *)(f->esp + 4);
      const void *buffer = *(char **)(f->esp + 8);
      unsigned size = *(unsigned *)(f->esp + 12);

      /* Buffer'ın işaret ettiği bellek alanının da güvenli olduğundan emin ol */
      check_valid_ptr (buffer);

      /* ADIM 6: Sadece FD 1 (Sistem Konsolu/Ekran) durumunu yönetiyoruz */
      if (fd == 1)
        {
          putbuf (buffer, size); /* Ekrana güvenle yazdırır */
          f->eax = size;         /* Başarıyla yazılan byte sayısını kullanıcıya döndür */
        }
      else
        {
          f->eax = -1;           /* Şimdilik diğer dosyalara yazmayı desteklemiyoruz */
        }
      break;

    default:
      /* Bilinmeyen bir çağrı gelirse sistemi çökertmemek için süreci kapat */
      printf ("%s: exit(-1)\n", thread_current ()->name);
      thread_exit ();
      break;
    }
}
