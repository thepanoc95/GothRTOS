#ifndef GOTH_LIBC_SYSCALL_H
#define GOTH_LIBC_SYSCALL_H

/*
 * Minimal Linux system-call bridge for GothLibC.
 *
 * GothLibC is freestanding: it does not call into the host C library.  The
 * hosted Lua build therefore reaches the kernel directly for the few
 * operations it needs (console/file I/O, the clock and process exit).
 */

#include <errno.h>
#include <stddef.h>

#if defined(__linux__)

#if defined(__x86_64__)
#define GOTH_SYS_read 0
#define GOTH_SYS_write 1
#define GOTH_SYS_close 3
#define GOTH_SYS_lseek 8
#define GOTH_SYS_rt_sigaction 13
#define GOTH_SYS_getpid 39
#define GOTH_SYS_exit 60
#define GOTH_SYS_wait4 61
#define GOTH_SYS_kill 62
#define GOTH_SYS_clock_gettime 228
#define GOTH_SYS_exit_group 231
#define GOTH_SYS_openat 257
#define GOTH_SYS_unlinkat 263
#define GOTH_SYS_renameat 264

static inline long goth_syscall6(long number, long first, long second,
                                 long third, long fourth, long fifth,
                                 long sixth)
{
    long result;
    register long r10 __asm__("r10") = fourth;
    register long r8 __asm__("r8") = fifth;
    register long r9 __asm__("r9") = sixth;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second), "d"(third),
                       "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return result;
}

#elif defined(__aarch64__)
#define GOTH_SYS_read 63
#define GOTH_SYS_write 64
#define GOTH_SYS_close 57
#define GOTH_SYS_lseek 62
#define GOTH_SYS_rt_sigaction 134
#define GOTH_SYS_getpid 172
#define GOTH_SYS_exit 93
#define GOTH_SYS_wait4 260
#define GOTH_SYS_kill 129
#define GOTH_SYS_clock_gettime 113
#define GOTH_SYS_exit_group 94
#define GOTH_SYS_openat 56
#define GOTH_SYS_unlinkat 35
#define GOTH_SYS_renameat 38

static inline long goth_syscall6(long number, long first, long second,
                                 long third, long fourth, long fifth,
                                 long sixth)
{
    register long x8 __asm__("x8") = number;
    register long x0 __asm__("x0") = first;
    register long x1 __asm__("x1") = second;
    register long x2 __asm__("x2") = third;
    register long x3 __asm__("x3") = fourth;
    register long x4 __asm__("x4") = fifth;
    register long x5 __asm__("x5") = sixth;

    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                     : "memory");
    return x0;
}

#else
#error "GothLibC system calls are not implemented for this architecture"
#endif

#define GOTH_AT_FDCWD (-100)
#define GOTH_AT_REMOVEDIR 0x200

#define GOTH_O_RDONLY 0
#define GOTH_O_WRONLY 1
#define GOTH_O_RDWR 2
#define GOTH_O_CREAT 0100
#define GOTH_O_EXCL 0200
#define GOTH_O_TRUNC 01000
#define GOTH_O_APPEND 02000

#define GOTH_SEEK_SET 0
#define GOTH_SEEK_CUR 1
#define GOTH_SEEK_END 2

#define GOTH_CLOCK_REALTIME 0
#define GOTH_CLOCK_MONOTONIC 1

struct goth_timespec {
    long tv_sec;
    long tv_nsec;
};

static inline long goth_sys_result(long result)
{
    if (result < 0 && result >= -4095) {
        errno = (int)-result;
        return -1;
    }
    return result;
}

static inline long goth_sys_read(int fd, void *buffer, size_t count)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_read, (long)fd,
                                         (long)buffer, (long)count, 0, 0, 0));
}

static inline long goth_sys_write(int fd, const void *buffer, size_t count)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_write, (long)fd,
                                         (long)buffer, (long)count, 0, 0, 0));
}

static inline long goth_sys_close(int fd)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_close, (long)fd, 0, 0, 0,
                                         0, 0));
}

static inline long goth_sys_lseek(int fd, long offset, int whence)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_lseek, (long)fd, offset,
                                         (long)whence, 0, 0, 0));
}

static inline long goth_sys_openat(int directory, const char *path, int flags,
                                   int mode)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_openat, (long)directory,
                                         (long)path, (long)flags, (long)mode,
                                         0, 0));
}

static inline long goth_sys_unlinkat(int directory, const char *path,
                                     int flags)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_unlinkat, (long)directory,
                                         (long)path, (long)flags, 0, 0, 0));
}

static inline long goth_sys_renameat(int old_directory, const char *old_path,
                                     int new_directory, const char *new_path)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_renameat,
                                         (long)old_directory, (long)old_path,
                                         (long)new_directory, (long)new_path,
                                         0, 0));
}

static inline long goth_sys_getpid(void)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_getpid, 0, 0, 0, 0, 0, 0));
}

static inline long goth_sys_clock_gettime(int clock,
                                          struct goth_timespec *value)
{
    return goth_sys_result(goth_syscall6(GOTH_SYS_clock_gettime, (long)clock,
                                         (long)value, 0, 0, 0, 0));
}

static inline void goth_sys_exit(int status)
{
    (void)goth_syscall6(GOTH_SYS_exit_group, (long)status, 0, 0, 0, 0, 0);
    (void)goth_syscall6(GOTH_SYS_exit, (long)status, 0, 0, 0, 0, 0);
    for (;;) {
    }
}

#else /* !__linux__ */

#error "GothLibC host services currently require Linux"

#endif /* __linux__ */

#endif /* GOTH_LIBC_SYSCALL_H */
