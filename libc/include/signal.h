#ifndef GOTH_LIBC_SIGNAL_H
#define GOTH_LIBC_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int sig_atomic_t;

#define SIGINT 2
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

void (*signal(int signal_number, void (*handler)(int)))(int);
int raise(int signal_number);

#ifdef __cplusplus
}
#endif

#endif
