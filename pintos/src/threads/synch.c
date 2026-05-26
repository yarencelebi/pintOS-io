/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void donate_priority (struct thread *t);
static void remove_donations_for_lock (struct thread *t, struct lock *lock);
static void update_priority_after_release (struct thread *t);

void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);
  sema->value = value;
  list_init (&sema->waiters);
}

void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      list_insert_ordered (&sema->waiters, &thread_current ()->elem,
                           thread_priority_greater, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
    {
      list_sort (&sema->waiters, thread_priority_greater, NULL);
      thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                  struct thread, elem));
    }
  sema->value++;
  intr_set_level (old_level);

  if (!intr_context ())
    {
      struct thread *cur = thread_current ();
      if (!list_empty (&ready_list))
        {
          struct thread *front =
            list_entry (list_begin (&ready_list), struct thread, elem);
          if (front->priority > cur->priority)
            thread_yield ();
        }
    }
}

static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;
  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

static void
donate_priority (struct thread *t)
{
  int depth = 0;
  while (t->waiting_lock != NULL && depth < 8)
    {
      struct thread *holder = t->waiting_lock->holder;
      if (holder == NULL)
        break;

      if (holder->priority >= t->priority)
        break;

      holder->priority = t->priority;

      if (holder->status == THREAD_READY)
        {
          list_remove (&holder->elem);
          list_insert_ordered (&ready_list, &holder->elem,
                               thread_priority_greater, NULL);
        }

      t = holder;
      depth++;
    }
}

static void
remove_donations_for_lock (struct thread *t, struct lock *lock)
{
  struct list_elem *e = list_begin (&t->donations);
  while (e != list_end (&t->donations))
    {
      struct thread *donor = list_entry (e, struct thread, donation_elem);
      if (donor->waiting_lock == lock)
        e = list_remove (e);
      else
        e = list_next (e);
    }
}

static void
update_priority_after_release (struct thread *t)
{
  int max = t->base_priority;
  struct list_elem *e;
  for (e = list_begin (&t->donations);
       e != list_end (&t->donations);
       e = list_next (e))
    {
      int p = list_entry (e, struct thread, donation_elem)->priority;
      if (p > max)
        max = p;
    }
  t->priority = max;
}

void
lock_acquire (struct lock *lock)
{
  struct thread *cur = thread_current ();

  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (!thread_mlfqs && lock->holder != NULL)
    {
      cur->waiting_lock = lock;
      list_push_back (&lock->holder->donations, &cur->donation_elem);
      donate_priority (cur);
    }

  sema_down (&lock->semaphore);
  cur->waiting_lock = NULL;
  lock->holder = cur;
}

bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;

  if (!thread_mlfqs)
    {
      remove_donations_for_lock (thread_current (), lock);
      update_priority_after_release (thread_current ());
    }

  sema_up (&lock->semaphore);
}

bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

struct semaphore_elem
{
  struct list_elem elem;
  struct semaphore semaphore;
};

void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

static bool
cond_priority_greater (const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED)
{
  struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
  struct thread *ta = list_entry (list_begin (&sa->semaphore.waiters),
                                  struct thread, elem);
  struct thread *tb = list_entry (list_begin (&sb->semaphore.waiters),
                                  struct thread, elem);
  return ta->priority > tb->priority;
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
    {
      list_sort (&cond->waiters, cond_priority_greater, NULL);
      sema_up (&list_entry (list_pop_front (&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
    }
}

void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
