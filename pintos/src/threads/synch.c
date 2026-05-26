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

/* This file is derived from source code for the Nachos instructional
   operating system.  The Nachos copyright notice and license terms
   appear below.

   "Copyright (c) 1992-1993 The Regents of the University of California.
   All rights reserved.

   Permission is hereby granted, without written notice or further
   redistribution of this source code, to copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject
   to the following conditions:

   The above copyright notice and permission notice shall appear in all
   copies or substantial portions of the Software and in supporting
   documentation.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING (BUT NOT LIMITED TO) WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY
   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE." */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer plus a list of threads.  Initializing a
   semaphore sets its value to VALUE and initializes its list of
   waiting threads to empty, allowing some initial threads to pass
   through the semaphore without blocking. */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Decrements SEMA's value
   if it is greater than zero.  If SEMA's value is zero, then the
   current thread blocks and adds itself to SEMA's list of waiting
   threads, then blocks its execution until it is awakened by some
   other thread calling sema_up.  All functions that call this
   function must not be called while interrupts are disabled. */
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

/* Up or "V" operation on a semaphore.  Increments SEMA's value and
   wakes up one thread of those waiting for the semaphore, if any. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
    {
      list_sort (&sema->waiters, thread_priority_greater, NULL);
      thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                  struct thread, elem));
    }
  sema->value++;
  
  if (!list_empty (&ready_list))
    {
      struct thread *front = list_entry (list_begin (&ready_list), struct thread, elem);
      if (front->priority > thread_current ()->priority)
        thread_yield ();
    }

  intr_set_level (old_level);
}

static void
donate_priority (void)
{
  struct thread *cur = thread_current ();
  struct thread *holder = cur->waiting_lock ? cur->waiting_lock->holder : NULL;

  int depth;
  for (depth = 0; depth < 8 && holder != NULL; depth++)
    {
      holder->priority = thread_get_effective_priority (holder);
      if (holder->status == THREAD_READY)
        {
          list_insert_ordered (&ready_list, &holder->elem,
                               thread_priority_greater, NULL);
        }
      holder = holder->waiting_lock ? holder->waiting_lock->holder : NULL;
    }
}

/* Initializes LOCK.  A lock can be held by at most a single thread at
   any given time.  Our locks are not "recursive", that is, it is an
   error for the holder to try to acquire a lock it is already
   holding. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK.  If a lock is already held by another thread,
   the current thread blocks until it becomes available. The lock's
   holder is set to the current thread when the lock is acquired.
   This function may sleep, so it must not be called within an
   interrupt handler. This function may be called with interrupts
   disabled, but interrupts will be turned back on if we sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();

  if (lock->holder != NULL)
    {
      cur->waiting_lock = lock;
      donate_priority ();
    }

  sema_down (&lock->semaphore);

  lock->holder = cur;
  cur->waiting_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock's holder is set to the current thread
   when the lock is acquired. */
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

/* Releases LOCK, which must be held by the current thread.
   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();

  thread_remove_donation (lock);
  cur->priority = thread_get_effective_priority (cur);

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;      /* List element. */
    struct semaphore semaphore; /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  The mutex loses the lock as
   a side effect of calling this function.  This function may
   sleep, so it must not be called within an interrupt handler.
   This function may be called with interrupts disabled, but
   interrupts will be turned back on if we sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  list_insert_ordered (&cond->waiters, &waiter.elem,
                       semaphore_elem_compare, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
    {
      list_sort (&cond->waiters, semaphore_elem_compare, NULL);
      sema_up (&list_entry (list_pop_front (&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK). */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/* Semaphore element comparison function. */
static bool
semaphore_elem_compare (const struct list_elem *a,
                        const struct list_elem *b,
                        void *aux UNUSED)
{
  struct semaphore_elem *se_a = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *se_b = list_entry (b, struct semaphore_elem, elem);
  
  struct thread *t_a = list_entry (list_begin (&se_a->semaphore.waiters),
                                   struct thread, elem);
  struct thread *t_b = list_entry (list_begin (&se_b->semaphore.waiters),
                                   struct thread, elem);
  
  if (list_empty (&se_a->semaphore.waiters))
    return false;
  if (list_empty (&se_b->semaphore.waiters))
    return true;
  
  return t_a->priority > t_b->priority;
}
