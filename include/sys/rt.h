#ifndef _RT_H
#define _RT_H

#include <stdint.h>

#define RTOS_VERSION_MAJOR 0
#define RTOS_VERSION_MINOR 0

static inline uint32_t goth_rtos_version(void)
{
    return (RTOS_VERSION_MAJOR << 16) | RTOS_VERSION_MINOR;
}

static inline const char *rtos_fetch_architecture(void)
{
#if defined(__i386__)
    return "x86";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__arm__)
    return "arm";
#elif defined(__aarch64__)
    return "aarch64";
#else
    return "unknown";
#endif


#define SYSTEM_PREF_OS_NAME "GothRTOS"
#define SYSTEM_PREF_OS_VERSION goth_rtos_version()
#define SYSTEM_PREF_OS_ARCHITECTURE rtos_fetch_architecture()
#define SYSTEM_PREF_OS_POINTER_WIDTH (sizeof(void *) * 8)

const char *rtos_fetch_os_name(void);
uint32_t rtos_fetch_os_version(void);

#define implementation static void r(v)

#endif // _RT_H 