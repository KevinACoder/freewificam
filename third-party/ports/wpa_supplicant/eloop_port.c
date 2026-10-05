/*
 * @file
 * @brief eloop implementation for the CMSIS-RTOS2 environment, after
 * src/utils/eloop poll design (eloop.c without the select fdset).
 *
 * There are no sockets on this port, so the select() loop reduces to
 * the timeout list plus the driver event queue in supp_main.c; a poll
 * slice keeps the loop self-contained. Timeout handlers always run on
 * the supplicant thread.
 *
 * Ported from the prior project's eloop_port.c ([S7a] §3.3.1: single
 * rewrite point - the ksleep slice sleep became wpa_msleep).
 *
 * @author zhugengyu
 */

#include <includes.h>

#include <sys/time.h>

#include "utils/common.h"
#include "dl_list.h"
#include "eloop.h"

#include "wpa_port_glue.h"
#include "wpa_port_api.h"

struct eloop_sock {
	int sock;
	void *eloop_data;
	void *user_data;
	eloop_sock_handler handler;
};

struct eloop_timeout {
	struct os_time time;
	void *eloop_data;
	void *user_data;
	eloop_timeout_handler handler;
	struct eloop_timeout *next;
};

struct eloop_sock_table {
	int count;
	struct eloop_sock *table;
	int changed;
};

struct eloop_data {
	struct eloop_sock_table readers;
	struct eloop_sock_table writers;
	struct eloop_sock_table exceptions;

	struct eloop_timeout *timeout;

	int signal_count;
	struct eloop_signal *signals;
	int terminate;
};

struct eloop_signal {
	int sig;
	void *user_data;
	eloop_signal_handler handler;
	int signaled;
};

static struct eloop_data eloop;

static void eloop_wake(void) {
	/* The event loop polls external queues at most 20 ms apart. */
}

void wpa_wake_loop(void) {
	eloop_wake();
}

static int clock_now(struct os_time *now) {
	return os_get_reltime((struct os_reltime *) now);
}

int eloop_init(void) {
	memset(&eloop, 0, sizeof(eloop));
	return 0;
}

int eloop_sock_requeue(void) {
	return 0;
}

static void eloop_sock_table_destroy(struct eloop_sock_table *table) {
	if (table != NULL) {
		os_free(table->table);
		table->table = NULL;
		table->count = 0;
	}
}

int eloop_register_read_sock(int sock, eloop_sock_handler handler,
    void *eloop_data, void *user_data) {
	return eloop_register_sock(sock, EVENT_TYPE_READ, handler,
	    eloop_data, user_data);
}

void eloop_unregister_read_sock(int sock) {
	eloop_unregister_sock(sock, EVENT_TYPE_READ);
}

static struct eloop_sock_table *eloop_sock_table(eloop_event_type type) {
	switch (type) {
	case EVENT_TYPE_READ:
		return &eloop.readers;
	case EVENT_TYPE_WRITE:
		return &eloop.writers;
	case EVENT_TYPE_EXCEPTION:
		return &eloop.exceptions;
	}
	return NULL;
}

int eloop_register_sock(int sock, eloop_event_type type,
    eloop_sock_handler handler, void *eloop_data, void *user_data) {
	struct eloop_sock_table *table = eloop_sock_table(type);
	struct eloop_sock *tmp;

	if (table == NULL) {
		return -1;
	}
	tmp = os_realloc(table->table,
	    (table->count + 1) * sizeof(struct eloop_sock));
	if (tmp == NULL) {
		return -1;
	}
	tmp[table->count].sock = sock;
	tmp[table->count].eloop_data = eloop_data;
	tmp[table->count].user_data = user_data;
	tmp[table->count].handler = handler;
	table->table = tmp;
	table->count++;
	table->changed = 1;
	eloop_wake();
	return 0;
}

void eloop_unregister_sock(int sock, eloop_event_type type) {
	struct eloop_sock_table *table = eloop_sock_table(type);
	int i;

	if (table == NULL || table->table == NULL) {
		return;
	}
	for (i = 0; i < table->count; i++) {
		if (table->table[i].sock == sock) {
			break;
		}
	}
	if (i == table->count) {
		return;
	}
	if (i != table->count - 1) {
		os_memmove(&table->table[i], &table->table[i + 1],
		    (table->count - i - 1) * sizeof(struct eloop_sock));
	}
	table->count--;
	table->changed = 1;
}

int eloop_register_timeout(unsigned int secs, unsigned int usecs,
    eloop_timeout_handler handler, void *eloop_data, void *user_data) {
	struct eloop_timeout *timeout, *tmp, *prev = NULL;

	timeout = os_zalloc(sizeof(*timeout));
	if (timeout == NULL) {
		return -1;
	}
	clock_now(&timeout->time);
	timeout->time.sec += secs;
	timeout->time.usec += usecs;
	while (timeout->time.usec >= 1000000) {
		timeout->time.sec++;
		timeout->time.usec -= 1000000;
	}
	timeout->eloop_data = eloop_data;
	timeout->user_data = user_data;
	timeout->handler = handler;
	timeout->next = NULL;

	if (eloop.timeout == NULL) {
		eloop.timeout = timeout;
	} else {
		prev = NULL;
		tmp = eloop.timeout;
		while (tmp != NULL) {
			if (os_time_before(&timeout->time, &tmp->time)) {
				break;
			}
			prev = tmp;
			tmp = tmp->next;
		}
		if (prev == NULL) {
			timeout->next = eloop.timeout;
			eloop.timeout = timeout;
		} else {
			timeout->next = prev->next;
			prev->next = timeout;
		}
	}

	/* a new head or an immediate timeout may shorten the wait */
	if (secs == 0 || prev == NULL) {
		eloop_wake();
	}
	return 0;
}

int eloop_cancel_timeout(eloop_timeout_handler handler, void *eloop_data,
    void *user_data) {
	struct eloop_timeout *timeout, *prev, *next;
	int removed = 0;

	prev = NULL;
	timeout = eloop.timeout;
	while (timeout != NULL) {
		next = timeout->next;

		if (timeout->handler == handler &&
		    (timeout->eloop_data == eloop_data ||
			eloop_data == ELOOP_ALL_CTX) &&
		    (timeout->user_data == user_data ||
			user_data == ELOOP_ALL_CTX)) {
			if (prev == NULL) {
				eloop.timeout = next;
			} else {
				prev->next = next;
			}
			os_free(timeout);
			removed++;
		} else {
			prev = timeout;
		}

		timeout = next;
	}

	return removed;
}

int eloop_cancel_timeout_one(eloop_timeout_handler handler,
    void *eloop_data, void *user_data, struct os_reltime *remaining) {
	struct eloop_timeout *timeout, *prev, *next;
	struct os_reltime now;
	int removed = 0;

	remaining->sec = remaining->usec = 0;

	os_get_reltime(&now);
	prev = NULL;
	timeout = eloop.timeout;
	while (timeout != NULL) {
		next = timeout->next;

		if (timeout->handler == handler &&
		    timeout->eloop_data == eloop_data &&
		    timeout->user_data == user_data) {
			if (prev == NULL) {
				eloop.timeout = next;
			} else {
				prev->next = next;
			}
			removed = 1;
			if (os_reltime_before(&now,
				(struct os_reltime *) &timeout->time)) {
				os_reltime_sub(
				    (struct os_reltime *) &timeout->time,
				    &now, remaining);
			}
			os_free(timeout);
			break;
		}
		prev = timeout;
		timeout = next;
	}

	return removed;
}

int eloop_is_timeout_registered(eloop_timeout_handler handler,
    void *eloop_data, void *user_data) {
	struct eloop_timeout *timeout;
	int found = 0;

	timeout = eloop.timeout;
	while (timeout != NULL) {
		if (timeout->handler == handler &&
		    timeout->eloop_data == eloop_data &&
		    timeout->user_data == user_data) {
			found = 1;
			break;
		}
		timeout = timeout->next;
	}

	return found;
}

static int eloop_reschedule_timeout(unsigned int req_secs,
    unsigned int req_usecs, eloop_timeout_handler handler, void *eloop_data,
    void *user_data, int only_shorten) {
	struct os_reltime now, requested, remaining;
	struct eloop_timeout *tmp;
	int res;

	tmp = eloop.timeout;
	while (tmp != NULL) {
		if (tmp->handler == handler && tmp->eloop_data == eloop_data &&
		    tmp->user_data == user_data) {
			requested.sec = req_secs;
			requested.usec = req_usecs;
			os_get_reltime(&now);
			os_reltime_sub(
			    (struct os_reltime *) &tmp->time, &now,
			    &remaining);
			res = only_shorten
				  ? os_reltime_before(&requested, &remaining)
				  : os_reltime_before(&remaining, &requested);
			if (res) {
				eloop_cancel_timeout(handler, eloop_data,
				    user_data);
				eloop_register_timeout(req_secs, req_usecs,
				    handler, eloop_data, user_data);
				return 1;
			}
			return 0;
		}
		tmp = tmp->next;
	}

	return -1;
}

int eloop_deplete_timeout(unsigned int req_secs, unsigned int req_usecs,
    eloop_timeout_handler handler, void *eloop_data, void *user_data) {

	return eloop_reschedule_timeout(req_secs, req_usecs, handler,
	    eloop_data, user_data, 1);
}

int eloop_replenish_timeout(unsigned int req_secs, unsigned int req_usecs,
    eloop_timeout_handler handler, void *eloop_data, void *user_data) {

	return eloop_reschedule_timeout(req_secs, req_usecs, handler,
	    eloop_data, user_data, 0);
}

int eloop_register_signal(int sig, eloop_signal_handler handler,
    void *user_data) {
	/* no POSIX signals in the kernel context; the terminate/
	 * reconfig registrations land here as no-ops */
	(void) sig;
	(void) handler;
	(void) user_data;
	return 0;
}

int eloop_register_signal_terminate(eloop_signal_handler handler,
    void *user_data) {
	(void) handler;
	(void) user_data;
	return 0;
}

int eloop_register_signal_reconfig(eloop_signal_handler handler,
    void *user_data) {
	(void) handler;
	(void) user_data;
	return 0;
}

void eloop_run(void) {
	while (!eloop.terminate) {
		struct os_time now, tv;
		unsigned int timeout_ms = 0u;
		if (eloop.timeout != NULL) {
			clock_now(&now);
			if (os_time_before(&now, &eloop.timeout->time)) {
				os_time_sub(&eloop.timeout->time, &now, &tv);
			} else {
				tv.sec = 0;
				tv.usec = 0;
			}
			timeout_ms = (unsigned int) tv.sec * 1000 +
				     (unsigned int) (tv.usec / 1000);
		}

		/* poll-wait in bounded slices: a semaphore wait interacts
		 * badly with the hostap callbacks on this kernel, and a
		 * short sleep keeps the eloop self-contained (the timeout
		 * expiry re-check below is authoritative) */
		{
			unsigned int slice = timeout_ms;

			if (slice == 0 || slice > 20) {
				slice = 20;
			}
			wpa_msleep(slice);
		}
		/* run everything that came due, outside the lock */
		for (unsigned budget = 0; budget < 16; budget++) {
			struct eloop_timeout *due = NULL;

			clock_now(&now);
			if (eloop.timeout != NULL &&
			    !os_time_before(&now, &eloop.timeout->time)) {
				due = eloop.timeout;
				eloop.timeout = due->next;
			}
			if (due == NULL) {
				break;
			}
			due->handler(due->eloop_data, due->user_data);
			os_free(due);
		}
		wpa_process_events();
		wpa_process_jobs();
	}
}

void eloop_terminate(void) {
	eloop.terminate = 1;
	eloop_wake();
}

void eloop_destroy(void) {
	struct eloop_timeout *timeout, *next;

	timeout = eloop.timeout;
	while (timeout != NULL) {
		next = timeout->next;
		os_free(timeout);
		timeout = next;
	}
	eloop.timeout = NULL;
	eloop_sock_table_destroy(&eloop.readers);
	eloop_sock_table_destroy(&eloop.writers);
	eloop_sock_table_destroy(&eloop.exceptions);
	os_free(eloop.signals);
	eloop.signals = NULL;
	eloop.signal_count = 0;
}

int eloop_terminated(void) {
	return eloop.terminate;
}

void eloop_wait_for_read_sock(int sock) {
	(void) sock;
}
