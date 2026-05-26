#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "filesys/file.h"

/* Açık dosya kaydı — her thread kendi listesini tutar */
struct file_descriptor
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

extern struct lock filesys_lock;

void syscall_init (void);
void exit (int status) NO_RETURN;

/* Adres doğrulama yardımcıları — process.c de kullanır */
void check_user_address (const void *addr);
void check_user_buffer  (const void *addr, unsigned size);
void check_user_string  (const char *str);

/* FD yardımcıları */
struct file *get_file_from_fd (int fd);
int          add_file_to_thread (struct file *f);

#endif /* userprog/syscall.h */
