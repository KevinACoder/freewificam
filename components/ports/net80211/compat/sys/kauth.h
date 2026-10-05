/*
 * @file
 * @brief kauth(9) stub: single-privilege context.
 */

#ifndef _COMPAT_SYS_KAUTH_H_
#define _COMPAT_SYS_KAUTH_H_

#include <sys/cdefs.h>
#include <sys/types.h>

/* Upstream's kauth_cred_t is a POINTER (sys/types.h: `typedef struct
 * kauth_cred *kauth_cred_t'), and the cred structure itself is a real
 * shape: audio(4) stores NULL into sc_cred and tests it. */
struct kauth_cred {
	u_int cr_refcnt;
	uid_t cr_uid;
	uid_t cr_euid;
	uid_t cr_svuid;
	gid_t cr_gid;
	gid_t cr_egid;
	gid_t cr_svgid;
	u_int cr_ngroups;
};

typedef struct kauth_cred *kauth_cred_t;

/* upstream's no-credential sentinels: audio(4) compares against
 * NOCRED when a credential was never installed */
#define NOCRED ((kauth_cred_t) -1)
#define FSCRED ((kauth_cred_t) -2)

#define kauth_authorize_generic(...) 1

/* The single privilege context: every credential request answers with
 * the one statically held cred, so hold/free are counters that never
 * gate destruction and the euid/egid pairs are the port's (root).
 * audio(4) records sc_cred at first open and stamps st_uid/st_gid from
 * these in audiostat(). */
kauth_cred_t kauth_cred_get(void);
kauth_cred_t kauth_cred_hold(kauth_cred_t);
void kauth_cred_free(kauth_cred_t);
uid_t kauth_cred_geteuid(kauth_cred_t);
gid_t kauth_cred_getegid(kauth_cred_t);

#endif /* _COMPAT_SYS_KAUTH_H_ */
