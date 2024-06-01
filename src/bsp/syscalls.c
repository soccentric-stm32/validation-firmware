/*
 * syscalls.c - newlib syscall retargeting
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  arm-none-eabi-gcc + newlib-nano
 * SRS:     GEN-001 build support; stdout/stderr go to the VCP (GEN-003).
 *
 * No general dynamic allocation: FreeRTOS heap_4 is static (section 5).  newlib's
 * floating-point formatting (printf "%f", linked with -u _printf_float) does
 * allocate small blocks, though, and with _sbrk failing the first test that
 * printed a float hit "REENT malloc succeeded" and the watchdog reset the
 * board.  _sbrk therefore serves a fixed arena for newlib, and __malloc_lock
 * keeps the tasks from using it at the same time.
 */

#include <errno.h>
#include <reent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bsp/uart3.h"

#define NEWLIB_ARENA_BYTES 8192U

int _write(int fd, const void *buf, size_t len)
{
    if ((fd == 1) || (fd == 2)) {
        hwval_uart3_write(buf, len);
        return (int)len;
    }
    errno = EBADF;
    return -1;
}

int _read(int fd, void *buf, size_t len)
{
    (void)buf;
    (void)len;
    (void)fd;
    errno = EBADF;
    return -1;
}

static uint8_t s_newlib_arena[NEWLIB_ARENA_BYTES] __attribute__((aligned(8)));
static size_t s_newlib_used;

void *_sbrk(ptrdiff_t incr)
{
    if ((incr < 0) || ((size_t)incr > (sizeof(s_newlib_arena) - s_newlib_used))) {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *block = &s_newlib_arena[s_newlib_used];

    s_newlib_used += (size_t)incr;
    return block;
}

void __malloc_lock(struct _reent *r)
{
    (void)r;
    vTaskSuspendAll();
}

void __malloc_unlock(struct _reent *r)
{
    (void)r;
    (void)xTaskResumeAll();
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    errno = EBADF;
    return -1;
}

void _exit(int code)
{
    (void)code;
    for (;;) {
        ;
    }
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}