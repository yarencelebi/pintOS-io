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

/* file_descriptor struct'ı syscall.h içinde tanımlı */

struct lock filesys_lock;

/* Tek bir pointer'ın kullanıcı alanında olduğunu kontrol eder */
void check_user_address(const void *addr) {
    if (addr == NULL || !is_user_vaddr(addr)) {
        exit(-1);
    }
}

/* Tüm buffer aralığının [addr, addr+size) kullanıcı alanında
   olduğunu kontrol eder. DÜZELTME: sadece başlangıç değil tümü. */
void check_user_buffer(const void *addr, unsigned size) {
    const char *p = (const char *)addr;
    unsigned i;
    for (i = 0; i < size; i++) {
        check_user_address((const void *)(p + i));
    }
}



void check_user_string(const char *str) {
    check_user_address(str);
    while (*str != '\0') {
        str++;
        check_user_address(str);
    }
}

/* FD numarasından dosya pointer'ı döndürür */
struct file *get_file_from_fd(int fd) {
    struct thread *t = thread_current();
    struct list_elem *e;

    for (e = list_begin(&t->open_files); e != list_end(&t->open_files); e = list_next(e)) {
        struct file_descriptor *fd_entry = list_entry(e, struct file_descriptor, elem);
        if (fd_entry->fd == fd)
            return fd_entry->file;
    }
    return NULL;
}

/* Açılan dosyayı thread'in listesine kaydeder, fd numarası döner */
int add_file_to_thread(struct file *f) {
    struct thread *t = thread_current();
    struct file_descriptor *fd_entry = malloc(sizeof(struct file_descriptor));

    if (fd_entry == NULL) return -1;

    fd_entry->file = f;
    fd_entry->fd = t->next_fd++;
    list_push_back(&t->open_files, &fd_entry->elem);

    return fd_entry->fd;
}

static void syscall_handler(struct intr_frame *f);

void syscall_init(void) {
    lock_init(&filesys_lock);
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f) {

    check_user_address(f->esp);
    int syscall_nr = *(int *)(f->esp);

    switch (syscall_nr) {

        case SYS_HALT:
            shutdown_power_off();
            break;

        case SYS_EXIT: {
            check_user_address(f->esp + 4);
            int status = *(int *)(f->esp + 4);
            exit(status);
            break;
        }

        case SYS_WRITE: {
            check_user_address(f->esp + 4);
            check_user_address(f->esp + 8);
            check_user_address(f->esp + 12);

            int fd        = *(int *)(f->esp + 4);
            void *buffer  = *(void **)(f->esp + 8);
            unsigned size = *(unsigned *)(f->esp + 12);


            check_user_buffer(buffer, size);

            if (fd == 1) {
                putbuf(buffer, size);
                f->eax = size;
            } else {

                struct file *file = get_file_from_fd(fd);
                if (file != NULL) {
                    lock_acquire(&filesys_lock);
                    f->eax = file_write(file, buffer, size);
                    lock_release(&filesys_lock);
                } else {
                    f->eax = -1;
                }
            }
            break;
        }

        case SYS_OPEN: {
            check_user_address(f->esp + 4);
            char *filename = *(char **)(f->esp + 4);


            check_user_string(filename);

            lock_acquire(&filesys_lock);
            struct file *f_ptr = filesys_open(filename);
            lock_release(&filesys_lock);

            if (f_ptr != NULL) {
                f->eax = add_file_to_thread(f_ptr);
            } else {
                f->eax = -1;
            }
            break;
        }

        case SYS_READ: {
         
            check_user_address(f->esp + 4);
            check_user_address(f->esp + 8);
            check_user_address(f->esp + 12);

            int fd_r        = *(int *)(f->esp + 4);
            char *buf_r     = *(char **)(f->esp + 8);
            unsigned size_r = *(unsigned *)(f->esp + 12);


            check_user_buffer(buf_r, size_r);

            if (fd_r == 0) { /* STDIN */
                for (unsigned i = 0; i < size_r; i++)
                    buf_r[i] = input_getc();
                f->eax = size_r;
            } else {
                struct file *file = get_file_from_fd(fd_r);
                if (file != NULL) {
                    lock_acquire(&filesys_lock);
                    f->eax = file_read(file, buf_r, size_r);
                    lock_release(&filesys_lock);
                } else {
                    f->eax = -1;
                }
            }
            break;
        }

        default:
            thread_exit();
    }
}


void exit(int status) {
    struct thread *t = thread_current();
    t->exit_status = status;
    thread_exit();
}
