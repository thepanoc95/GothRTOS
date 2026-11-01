/* #if defined(__i386__)
    #include "start_i386.h"
#elif defined(__x86_64__)
    #include "start_x86_64.h"
#elif defined(__arm__)
    #include "start_arm.h"
#endif
*/

#include <sys/rt.h>
#include <stddef.h>

void _start(void)
{
    extern int main(int argc, char *argv[]);
    extern void goth_rtos_exit(int status);
    int exit_code = main(0, NULL);
    boot_ldr(SYSTEM_PREF_OS_NAME, SYSTEM_PREF_OS_VERSION,
             SYSTEM_PREF_OS_ARCHITECTURE, SYSTEM_PREF_OS_POINTER_WIDTH);
    goth_rtos_exit(exit_code);
}