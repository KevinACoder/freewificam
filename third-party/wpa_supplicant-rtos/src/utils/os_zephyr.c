/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <sys/time.h>

#include <zephyr/posix/time.h>
#include <zephyr/random/random.h>

#include "includes.h"
#include "os.h"

void os_sleep(os_time_t sec, os_time_t usec)
{
    if (sec)
    {
        k_sleep(K_SECONDS(sec));
    }
    if (usec)
    {
        k_usleep(usec);
    }
}

int os_get_time(struct os_time *t)
{
    int res;
    struct timeval tv;

    res     = gettimeofday(&tv, NULL);
    t->sec  = tv.tv_sec;
    t->usec = tv.tv_usec;
    return res;
}

int os_get_reltime(struct os_reltime *t)
{
#if defined(CLOCK_BOOTTIME)
    static clockid_t clock_id = CLOCK_BOOTTIME;
#elif defined(CLOCK_MONOTONIC)
    static clockid_t clock_id = CLOCK_MONOTONIC;
#else
    static clockid_t clock_id = CLOCK_REALTIME;
#endif
    struct timespec ts;
    int res;

    if (TEST_FAIL())
    {
        return -1;
    }

    while (1)
    {
        res = clock_gettime(clock_id, &ts);
        if (res == 0)
        {
            t->sec  = ts.tv_sec;
            t->usec = ts.tv_nsec / 1000;
            return 0;
        }
        switch (clock_id)
        {
#ifdef CLOCK_BOOTTIME
            case CLOCK_BOOTTIME:
                clock_id = CLOCK_MONOTONIC;
                break;
#endif
#ifdef CLOCK_MONOTONIC
            case CLOCK_MONOTONIC:
                clock_id = CLOCK_REALTIME;
                break;
#endif
            case CLOCK_REALTIME:
                return -1;
        }
    }
}

int os_mktime(int year, int month, int day, int hour, int min, int sec, os_time_t *t)
{
    struct tm tm, *tm1;
    time_t t_local, t1, t2;
    os_time_t tz_offset;

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || min < 0 || min > 59 ||
        sec < 0 || sec > 60)
    {
        return -1;
    }

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;

    t_local = mktime(&tm);

    /* figure out offset to UTC */
    tm1 = localtime(&t_local);
    if (tm1)
    {
        t1  = mktime(tm1);
        tm1 = gmtime(&t_local);
        if (tm1)
        {
            t2        = mktime(tm1);
            tz_offset = t2 - t1;
        }
        else
        {
            tz_offset = 0;
        }
    }
    else
    {
        tz_offset = 0;
    }

    *t = (os_time_t)t_local - tz_offset;
    return 0;
}

int os_gmtime(os_time_t t, struct os_tm *tm)
{
    struct tm *tm2;
    time_t t2 = t;

    tm2 = gmtime(&t2);
    if (tm2 == NULL)
    {
        return -1;
    }
    tm->sec   = tm2->tm_sec;
    tm->min   = tm2->tm_min;
    tm->hour  = tm2->tm_hour;
    tm->day   = tm2->tm_mday;
    tm->month = tm2->tm_mon + 1;
    tm->year  = tm2->tm_year + 1900;
    return 0;
}

int os_daemonize(const char *pid_file)
{
    return -1;
}

void os_daemonize_terminate(const char *pid_file)
{
}

int os_get_random(unsigned char *buf, size_t len)
{
    sys_rand_get(buf, len);

    return 0;
}

unsigned long os_random(void)
{
    return sys_rand32_get();
}

char *os_rel2abs_path(const char *rel_path)
{
    return NULL; /* strdup(rel_path) can be used here */
}

int os_program_init(void)
{
    return 0;
}

void os_program_deinit(void)
{
}

int os_setenv(const char *name, const char *value, int overwrite)
{
    return -1;
}

int os_unsetenv(const char *name)
{
    return -1;
}

char *os_readfile(const char *name, size_t *len)
{
    return NULL;
}

int os_fdatasync(FILE *stream)
{
    return 0;
}

char *os_strdup(const char *s)
{
    size_t len;
    char *d;

    len = os_strlen(s);
    d   = os_malloc(len + 1);
    if (d == NULL)
    {
        return NULL;
    }
    os_memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

void *os_memdup(const void *src, size_t len)
{
    void *r = os_malloc(len);

    if (r && src)
    {
        os_memcpy(r, src, len);
    }
    return r;
}

void *os_malloc(size_t size)
{
    return k_malloc(size);
}

void os_free(void *ptr)
{
    k_free(ptr);
}

void *os_realloc(void *ptr, size_t newsize)
{
    void *p;

    if (newsize == 0)
    {
        os_free(ptr);
        return NULL;
    }

    if (ptr == NULL)
    {
        return os_malloc(newsize);
    }

    p = os_zalloc(newsize);

    if (p)
    {
        if (ptr != NULL)
        {
            memcpy(p, ptr, newsize);
            os_free(ptr);
        }
    }

    return p;
}

void *os_zalloc(size_t size)
{
    void *p = k_malloc(size);

    if (p != NULL)
    {
        (void)memset(p, 0, size);
    }
    return p;
}

size_t os_strlcpy(char *dest, const char *src, size_t siz)
{
    const char *s = src;
    size_t left   = siz;

    if (left)
    {
        /* Copy string up to the maximum size of the dest buffer */
        while (--left != 0)
        {
            if ((*dest++ = *s++) == '\0')
            {
                break;
            }
        }
    }

    if (left == 0)
    {
        /* Not enough room for the string; force NUL-termination */
        if (siz != 0)
        {
            *dest = '\0';
        }
        while (*s++)
        {
            ; /* determine total src string length */
        }
    }

    return s - src - 1;
}

int os_exec(const char *program, const char *arg, int wait_completion)
{
    return -1;
}

/* Duplicate S, returning an identical malloc'd string.  */
char *__strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    void *new  = malloc(len);

    if (new == NULL)
    {
        return NULL;
    }
    return (char *)memcpy(new, s, len);
}

int os_strcasecmp(const char *s1, const char *s2)
{
    /*
     * Ignoring case is not required for main functionality, so just use
     * the case sensitive version of the function.
     */
    return os_strcmp(s1, s2);
}

int os_strncasecmp(const char *s1, const char *s2, size_t n)
{
    /*
     * Ignoring case is not required for main functionality, so just use
     * the case sensitive version of the function.
     */
    return os_strncmp(s1, s2, n);
}

int __attribute__((weak)) _link(const char *path1, const char *path2);
int __attribute__((weak)) _link(const char *path1, const char *path2)
{
    errno = ENOSYS;
    return -1;
}

int __attribute__((weak)) _unlink(const char *path);
int __attribute__((weak)) _unlink(const char *path)
{
    errno = ENOSYS;
    return -1;
}

