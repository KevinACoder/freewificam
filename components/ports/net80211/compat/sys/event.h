/*
 * @file
 * @brief knote(9)/filterops shell: the usb device node registers one
 * read filter; the queue behind it never wakes here.
 */

#ifndef _COMPAT_SYS_EVENT_H_
#define _COMPAT_SYS_EVENT_H_

#include <sys/cdefs.h>

#define FILTEROP_ISFD	0x0001
#define EVFILT_READ	(-1)
#define EVFILT_WRITE	(-2)

/* kqueue note flags: only the one the audio(4) selnotify() calls carry
 * (upstream value); the queue behind it never wakes here */
#define NOTE_SUBMIT	0x01000000U

struct knote;

struct filterops {
	int f_flags;
	int (*f_attach)(struct knote *);
	void (*f_detach)(struct knote *);
	int (*f_event)(struct knote *, long);
};

struct knote {
	void *kn_hook;
	const struct filterops *kn_fop;
	int kn_filter;
	int kn_flags;
	int kn_data;
};

#endif /* _COMPAT_SYS_EVENT_H_ */
