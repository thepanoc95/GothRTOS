#include <gothrtos/libc.h>

#include "goth_syscall.h"

extern int main(int argc, char **argv);

int goth_libc_start(int argc, char **argv, char **environment)
{
    int status;

    goth_libc_environ = environment;
    if (goth_libc_heap_init_auto() != 0) {
        static const char message[] =
            "gothc: unable to initialise the heap\n";
        (void)goth_sys_write(2, message, sizeof(message) - 1U);
        goth_sys_exit(127);
    }

    status = main(argc, argv);
    goth_sys_exit(status);
    return status;
}
