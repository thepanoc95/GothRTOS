#ifndef GOTHRTOS_KLOG_H
#define GOTHRTOS_KLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Kernel log (dmesg) for the GothRTOS nanokernel.
 *
 * Messages are kept in a fixed-size static ring and mirrored to the platform
 * console. Timestamps are taken from the platform monotonic clock, so the
 * output has the same shape as a Linux dmesg:
 *
 *     [    0.000000] GothRTOS 0.0-indev booting
 *
 * Nothing here allocates: the ring and its records live in .bss. */

#define GOTH_KLOG_MESSAGE_SIZE 192U
#define GOTH_KLOG_RECORDS 64U

typedef enum {
    GOTH_LOG_EMERG = 0,
    GOTH_LOG_ALERT,
    GOTH_LOG_CRIT,
    GOTH_LOG_ERR,
    GOTH_LOG_WARNING,
    GOTH_LOG_NOTICE,
    GOTH_LOG_INFO,
    GOTH_LOG_DEBUG
} goth_log_level_t;

typedef struct {
    uint64_t timestamp_ns;
    goth_log_level_t level;
    char message[GOTH_KLOG_MESSAGE_SIZE];
} goth_log_record_t;

/* Initializes the ring once. Repeated calls keep the original boot time so a
 * loader that initializes early and the kernel that initializes later share
 * one timeline. */
void goth_klog_init(void);
bool goth_klog_is_initialized(void);

/* Console threshold: messages with a level at or below this value are also
 * written to the console. The default is GOTH_LOG_INFO. */
void goth_klog_set_console_level(goth_log_level_t level);
goth_log_level_t goth_klog_console_level(void);

/* Stores a message in the ring and, depending on the console level, prints
 * it. `printf` understands %s %c %d %i %u %x %X %p %% with the l/ll/z length
 * modifiers and simple width/zero-padding. */
void goth_klog_emit(goth_log_level_t level, const char *message);
void goth_klog_printf(goth_log_level_t level, const char *format, ...);

const goth_log_record_t *goth_klog_at(size_t index);
size_t goth_klog_count(void);
size_t goth_klog_capacity(void);
uint64_t goth_klog_dropped(void);
void goth_klog_clear(void);

const char *goth_klog_level_name(goth_log_level_t level);

/* Formats one retained record as "[ seconds.microseconds] message" and
 * returns the number of bytes written (excluding the terminator). */
size_t goth_klog_line(size_t index, char *buffer, size_t capacity);

/* Registers the ring with the memory-protection subsystem. */
void goth_klog_register_memory(void);

#endif
