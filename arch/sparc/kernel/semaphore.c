/* $Id: semaphore.c,v 1.3 2000/10/14 10:09:00 davem Exp $
 * Generic semaphore code. Buyer beware. Do your own
 * specific changes in <asm/semaphore-helper.h>
 */

#include <linux/sched.h>
#include <asm/semaphore-helper.h>

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to sleep, while the "waking" variable is
 * incremented when the "up()" code goes to wake up waiting
 * processes.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * waking_non_zero() (from asm/semaphore.h) must execute
 * atomically.
 *
 * When __up() is called, the count was negative before
 * incrementing it, and we need to wake up somebody.
 *
 * This routine adds one to the count of processes that need to
 * wake up and exit.  ALL waiting processes actually wake up but
 * only the one that gets to the "waking" field first will gate
 * through and acquire the semaphore.  The others will go back
 * to sleep.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */
void __up(struct semaphore *sem)
{
	wake_one_more(sem);
	wake_up(&sem->wait);
}

/*
 * Perform the "down" function.  Return zero for semaphore acquired,
 * return negative for signalled out of the function.
 *
 * If called from __down, the return is ignored and the wait loop is
 * not interruptible.  This means that a task waiting on a semaphore
 * using "down()" cannot be killed until someone does an "up()" on
 * the semaphore.
 *
 * If called from __down_interruptible, the return value gets checked
 * upon return.  If the return value is negative then the task continues
 * with the negative value in the return register (it can be tested by
 * the caller).
 *
 * Either form may be used in conjunction with "up()".
 *
 */

#define DOWN_VAR				\
	struct task_struct *tsk = current;	\
	DECLARE_WAITQUEUE(wait, tsk);

#define DOWN_HEAD(task_state)						\
									\
									\
	tsk->state = (task_state);					\
	add_wait_queue(&sem->wait, &wait);				\
									\
	/*								\
	 * Ok, we're set up.  sem->count is known to be less than zero	\
	 * so we must wait.						\
	 *								\
	 * We can let go the lock for purposes of waiting.		\
	 * We re-acquire it after awaking so as to protect		\
	 * all semaphore operations.					\
	 *								\
	 * If "up()" is called before we call waking_non_zero() then	\
	 * we will catch it right away.  If it is called later then	\
	 * we will have to go through a wakeup cycle to catch it.	\
	 *								\
	 * Multiple waiters contend for the semaphore lock to see	\
	 * who gets to gate through and who has to wait some more.	\
	 */								\
	for (;;) {

#define DOWN_TAIL(task_state)			\
		tsk->state = (task_state);	\
	}					\
	tsk->state = TASK_RUNNING;		\
	remove_wait_queue(&sem->wait, &wait);

void __down(struct semaphore * sem)
{
	DOWN_VAR
	DOWN_HEAD(TASK_UNINTERRUPTIBLE)
	if (waking_non_zero(sem))
		break;
	schedule();
	DOWN_TAIL(TASK_UNINTERRUPTIBLE)
}

int __down_interruptible(struct semaphore * sem)
{
	int ret = 0;
	DOWN_VAR
	DOWN_HEAD(TASK_INTERRUPTIBLE)

	ret = waking_non_zero_interruptible(sem, tsk);
	if (ret)
	{
		if (ret == 1)
			/* ret != 0 only if we get interrupted -arca */
			ret = 0;
		break;
	}
	schedule();
	DOWN_TAIL(TASK_INTERRUPTIBLE)
	return ret;
}

int __down_trylock(struct semaphore * sem)
{
	return waking_non_zero_trylock(sem);
}

/* rw mutexes
 * Implemented by Jakub Jelinek (jakub@redhat.com) based on
 * i386 implementation by Ben LaHaise (bcrl@redhat.com).
 */

extern inline int ldstub(unsigned char *p)
{
	int ret;
	asm volatile("ldstub %1, %0" : "=r" (ret) : "m" (*p) : "memory");
	return ret;
}

void down_read_failed_biased(struct rw_semaphore *sem)
{
	DOWN_VAR

	add_wait_queue(&sem->wait, &wait);	/* put ourselves at the head of the list */

	for (;;) {
		if (!ldstub(&sem->read_not_granted))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->read_not_granted)
			schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

void down_write_failed_biased(struct rw_semaphore *sem)
{
	DOWN_VAR

	add_wait_queue_exclusive(&sem->write_bias_wait, &wait); /* put ourselves at the end of the list */

	for (;;) {
		if (!ldstub(&sem->write_not_granted))
			break;
		set_task_state(tsk, TASK_UNINTERRUPTIBLE | TASK_EXCLUSIVE);
		if (sem->write_not_granted)
			schedule();
	}

	remove_wait_queue(&sem->write_bias_wait, &wait);
	tsk->state = TASK_RUNNING;

	/* if the lock is currently unbiased, awaken the sleepers
	 * FIXME: this wakes up the readers early in a bit of a
	 * stampede -> bad!
	 */
	if (sem->count >= 0)
		wake_up(&sem->wait);
}

/* Wait for the lock to become unbiased.  Readers
 * are non-exclusive. =)
 */
void down_read_failed(struct rw_semaphore *sem)
{
	DOWN_VAR

	__up_read(sem); /* this takes care of granting the lock */

	add_wait_queue(&sem->wait, &wait);

	while (sem->count < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (sem->count >= 0)
			break;
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

/* Wait for the lock to become unbiased. Since we're
 * a writer, we'll make ourselves exclusive.
 */
void down_write_failed(struct rw_semaphore *sem)
{
	DOWN_VAR

	__up_write(sem);	/* this takes care of granting the lock */

	add_wait_queue_exclusive(&sem->wait, &wait);

	while (sem->count < 0) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE | TASK_EXCLUSIVE);
		if (sem->count >= 0)
			break;  /* we must attempt to acquire or bias the lock */
		schedule();
	}

	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
}

void __rwsem_wake(struct rw_semaphore *sem, unsigned long readers)
{
	if (readers) {
		/* Due to lame ldstub we don't do here
		   a BUG() consistency check */
		sem->read_not_granted = 0;
		wake_up(&sem->wait);
	} else {
		sem->write_not_granted = 0;
		wake_up(&sem->write_bias_wait);
	}
}
