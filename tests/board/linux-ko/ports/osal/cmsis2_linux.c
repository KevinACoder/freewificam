/*
 * @file
 * @brief The CMSIS-RTOS2 backend of the net80211 port, on Linux kernel services.
 *
 * The port's osal calls one small CMSIS face (mutex, semaphore, thread id,
 * tick, delay).  This file backs that face with the kernel's own primitives:
 *
 *   osMutex       recursive mutex: <linux/mutex.h> plus an owner/depth pair.
 *                 A re-entrant acquire by the owner only bumps the depth; the
 *                 kernel mutex stays locked until the outermost release.
 *   osSemaphore   counting semaphore: <linux/semaphore.h>, with the CMSIS
 *                 timeout in milliseconds and osWaitForever mapped to down().
 *   thread id     current.
 *   tick base     milliseconds (jiffies_to_msecs); osKernelGetTickFreq() = 1000,
 *                 which is the unit hz is set to by the port's osal.
 *
 * The port's own bookkeeping (wlan_mutex_* in osal_cmsis2.c) also tracks owner
 * and depth; that is the port's serializer contract, not a second lock.  This
 * backend's recursion exists so a re-entrant acquire does not deadlock the
 * kernel mutex.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/limits.h>

#include <cmsis_os2.h>

struct wlan_linux_mutex {
	struct mutex	lock;
	struct task_struct *owner;
	unsigned int	depth;
};

struct wlan_linux_sem {
	struct semaphore sem;
};

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
	struct wlan_linux_mutex *m;

	(void)attr;
	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (m == NULL) {
		return NULL;
	}
	mutex_init(&m->lock);
	return (osMutexId_t)m;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
	struct wlan_linux_mutex *m = (struct wlan_linux_mutex *)mutex_id;

	if (m == NULL) {
		return osErrorParameter;
	}
	if (m->owner == current) {
		m->depth++;
		return osOK;
	}
	if (timeout == osWaitForever) {
		mutex_lock(&m->lock);
	} else {
		unsigned long deadline = jiffies +
		    msecs_to_jiffies(timeout);

		while (!mutex_trylock(&m->lock)) {
			if (time_after_eq(jiffies, deadline)) {
				return osErrorTimeout;
			}
			if (msleep_interruptible(1) != 0) {
				return osErrorTimeout;
			}
		}
	}
	m->owner = current;
	m->depth = 1;
	return osOK;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
	struct wlan_linux_mutex *m = (struct wlan_linux_mutex *)mutex_id;

	if (m == NULL || m->owner != current || m->depth == 0) {
		return osErrorResource;
	}
	if (--m->depth != 0) {
		return osOK;
	}
	m->owner = NULL;
	mutex_unlock(&m->lock);
	return osOK;
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
	struct wlan_linux_mutex *m = (struct wlan_linux_mutex *)mutex_id;

	if (m == NULL) {
		return osErrorParameter;
	}
	mutex_destroy(&m->lock);
	kfree(m);
	return osOK;
}

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
    const osSemaphoreAttr_t *attr)
{
	struct wlan_linux_sem *s;

	(void)max_count;
	(void)attr;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL) {
		return NULL;
	}
	sema_init(&s->sem, initial_count);
	return (osSemaphoreId_t)s;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
	struct wlan_linux_sem *s = (struct wlan_linux_sem *)semaphore_id;

	if (s == NULL) {
		return osErrorParameter;
	}
	if (timeout == osWaitForever) {
		down(&s->sem);
		return osOK;
	}
	if (down_timeout(&s->sem, msecs_to_jiffies(timeout)) != 0) {
		return osErrorTimeout;
	}
	return osOK;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
	struct wlan_linux_sem *s = (struct wlan_linux_sem *)semaphore_id;

	if (s == NULL) {
		return osErrorParameter;
	}
	up(&s->sem);
	return osOK;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
	struct wlan_linux_sem *s = (struct wlan_linux_sem *)semaphore_id;

	if (s == NULL) {
		return osErrorParameter;
	}
	kfree(s);
	return osOK;
}

osThreadId_t osThreadGetId(void)
{
	return (osThreadId_t)current;
}

uint32_t osKernelGetTickCount(void)
{
	return (uint32_t)jiffies_to_msecs(jiffies);
}

uint32_t osKernelGetTickFreq(void)
{
	return 1000;
}

osStatus_t osDelay(uint32_t ticks)
{
	if (ticks != 0) {
		msleep(ticks);
	}
	return osOK;
}
