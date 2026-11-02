#include <signal.h>
#include <stdlib.h>

#define GOTH_LIBC_SIGNAL_COUNT 32

static void (*handlers[GOTH_LIBC_SIGNAL_COUNT])(int);

void (*signal(int signal_number, void (*handler)(int)))(int)
{
    void (*previous)(int);

    if (signal_number <= 0 || signal_number >= GOTH_LIBC_SIGNAL_COUNT) {
        return SIG_ERR;
    }
    previous = handlers[signal_number];
    handlers[signal_number] = handler;
    return previous;
}

int raise(int signal_number)
{
    void (*handler)(int);

    if (signal_number <= 0 || signal_number >= GOTH_LIBC_SIGNAL_COUNT) {
        return -1;
    }
    handler = handlers[signal_number];
    if (handler == SIG_IGN) {
        return 0;
    }
    if (handler == SIG_DFL || handler == NULL) {
        exit(128 + signal_number);
    }
    handler(signal_number);
    return 0;
}
