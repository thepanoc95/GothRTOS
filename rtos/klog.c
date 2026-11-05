#include "gothrtos/klog.h"

#include "gothrtos/mem.h"
#include "gothrtos/pexpert.h"

#include <stdarg.h>

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} log_output_t;

static goth_log_record_t records[GOTH_KLOG_RECORDS];
static size_t record_next;
static size_t record_count;
static uint64_t record_total;
static uint64_t boot_ns;
static bool boot_clock_valid;
static goth_log_level_t console_level;
static bool log_initialized;

static void output_char(log_output_t *output, char value)
{
    if (output->capacity > 0U &&
        output->length + 1U < output->capacity) {
        output->buffer[output->length] = value;
    }
    if (output->length + 1U < output->capacity || output->capacity == 0U) {
        output->length++;
    } else {
        output->length = output->capacity - 1U;
    }
}

static void output_text(log_output_t *output, const char *text)
{
    if (text == NULL) {
        text = "(null)";
    }
    while (*text != '\0') {
        output_char(output, *text++);
    }
}

static void output_repeat(log_output_t *output, char value, size_t count)
{
    while (count-- > 0U) {
        output_char(output, value);
    }
}

static size_t unsigned_digits(char *buffer, uint64_t value, unsigned base,
                              bool upper)
{
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reverse[32];
    size_t count = 0U;
    size_t index;

    if (base < 2U || base > 16U) {
        base = 10U;
    }
    do {
        reverse[count++] = alphabet[value % base];
        value /= base;
    } while (value != 0U && count < sizeof(reverse));

    for (index = 0U; index < count; index++) {
        buffer[index] = reverse[count - index - 1U];
    }
    return count;
}

static void output_unsigned(log_output_t *output, uint64_t value,
                            unsigned base, bool upper, int width, char pad,
                            bool left, bool negative, bool prefix_space)
{
    char digits[32];
    char sign = '\0';
    size_t count;
    size_t total;

    count = unsigned_digits(digits, value, base, upper);
    if (negative) {
        sign = '-';
    } else if (prefix_space) {
        sign = ' ';
    }
    total = count + (sign != '\0' ? 1U : 0U);

    if (!left && width > 0 && (size_t)width > total) {
        output_repeat(output, pad, (size_t)width - total);
    }
    if (sign != '\0') {
        output_char(output, sign);
    }
    {
        size_t index;

        for (index = 0U; index < count; index++) {
            output_char(output, digits[index]);
        }
    }
    if (left && width > 0 && (size_t)width > total) {
        output_repeat(output, ' ', (size_t)width - total);
    }
}

static void output_signed(log_output_t *output, int64_t value, int width,
                          char pad, bool left, bool prefix_space)
{
    bool negative = value < 0;
    uint64_t magnitude;

    if (negative) {
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    } else {
        magnitude = (uint64_t)value;
    }
    output_unsigned(output, magnitude, 10U, false, width, pad, left,
                    negative, prefix_space);
}

static void output_string(log_output_t *output, const char *text, int width,
                          bool left)
{
    size_t length = 0U;

    if (text == NULL) {
        text = "(null)";
    }
    while (text[length] != '\0') {
        length++;
    }
    if (!left && width > 0 && (size_t)width > length) {
        output_repeat(output, ' ', (size_t)width - length);
    }
    output_text(output, text);
    if (left && width > 0 && (size_t)width > length) {
        output_repeat(output, ' ', (size_t)width - length);
    }
}

static void output_pointer(log_output_t *output, const void *pointer)
{
    uintptr_t value = (uintptr_t)pointer;
    char digits[32];
    size_t count = unsigned_digits(digits, (uint64_t)value, 16U, false);
    size_t index;

    output_text(output, "0x");
    for (index = 0U; index < count; index++) {
        output_char(output, digits[index]);
    }
}

static void terminate_output(log_output_t *output)
{
    if (output->capacity > 0U) {
        size_t position = output->length;

        if (position >= output->capacity) {
            position = output->capacity - 1U;
        }
        output->buffer[position] = '\0';
        output->length = position;
    }
}

static size_t format_message(char *buffer, size_t capacity,
                             const char *format, va_list arguments)
{
    log_output_t output;
    bool left;
    bool zero_pad;
    bool prefix_space;
    int width;

    output.buffer = buffer;
    output.capacity = capacity;
    output.length = 0U;

    while (format != NULL && *format != '\0') {
        if (*format != '%') {
            output_char(&output, *format++);
            continue;
        }
        format++;
        if (*format == '%') {
            output_char(&output, '%');
            format++;
            continue;
        }

        left = false;
        zero_pad = false;
        prefix_space = false;
        while (*format == '-' || *format == '0' || *format == '+' ||
               *format == ' ') {
            if (*format == '-') {
                left = true;
            } else if (*format == '0') {
                zero_pad = true;
            } else if (*format == ' ') {
                prefix_space = true;
            }
            format++;
        }

        width = 0;
        while (*format >= '0' && *format <= '9') {
            if (width < 1000) {
                width = width * 10 + (*format - '0');
            }
            format++;
        }
        if (*format == '.') {
            format++;
            while (*format >= '0' && *format <= '9') {
                format++;
            }
        }

        if (*format == 'l') {
            format++;
            if (*format == 'l') {
                format++;
                switch (*format++) {
                case 'd':
                case 'i':
                    output_signed(&output, va_arg(arguments, long long),
                                  width, zero_pad ? '0' : ' ', left,
                                  prefix_space);
                    break;
                case 'u':
                    output_unsigned(&output,
                                    va_arg(arguments, unsigned long long),
                                    10U, false, width, zero_pad ? '0' : ' ',
                                    left, false, false);
                    break;
                case 'x':
                case 'X':
                    output_unsigned(&output,
                                    va_arg(arguments, unsigned long long),
                                    16U, format[-1] == 'X', width,
                                    zero_pad ? '0' : ' ', left, false,
                                    false);
                    break;
                default:
                    output_char(&output, '%');
                    output_char(&output, format[-1]);
                    break;
                }
            } else {
                switch (*format++) {
                case 'd':
                case 'i':
                    output_signed(&output, va_arg(arguments, long), width,
                                  zero_pad ? '0' : ' ', left, prefix_space);
                    break;
                case 'u':
                    output_unsigned(&output, va_arg(arguments, unsigned long),
                                    10U, false, width, zero_pad ? '0' : ' ',
                                    left, false, false);
                    break;
                case 'x':
                case 'X':
                    output_unsigned(&output, va_arg(arguments, unsigned long),
                                    16U, format[-1] == 'X', width,
                                    zero_pad ? '0' : ' ', left, false,
                                    false);
                    break;
                default:
                    output_char(&output, '%');
                    output_char(&output, format[-1]);
                    break;
                }
            }
        } else if (*format == 'z') {
            format++;
            switch (*format++) {
            case 'd':
            case 'i':
                output_signed(&output, (int64_t)va_arg(arguments, size_t),
                              width, zero_pad ? '0' : ' ', left,
                              prefix_space);
                break;
            case 'u':
                output_unsigned(&output, va_arg(arguments, size_t), 10U,
                                false, width, zero_pad ? '0' : ' ', left,
                                false, false);
                break;
            case 'x':
            case 'X':
                output_unsigned(&output, va_arg(arguments, size_t), 16U,
                                format[-1] == 'X', width,
                                zero_pad ? '0' : ' ', left, false, false);
                break;
            default:
                output_char(&output, '%');
                output_char(&output, format[-1]);
                break;
            }
        } else {
            switch (*format++) {
            case 'd':
            case 'i':
                output_signed(&output, va_arg(arguments, int), width,
                              zero_pad ? '0' : ' ', left, prefix_space);
                break;
            case 'u':
                output_unsigned(&output, va_arg(arguments, unsigned int),
                                10U, false, width, zero_pad ? '0' : ' ',
                                left, false, false);
                break;
            case 'x':
            case 'X':
                output_unsigned(&output, va_arg(arguments, unsigned int),
                                16U, format[-1] == 'X', width,
                                zero_pad ? '0' : ' ', left, false, false);
                break;
            case 'p':
                output_pointer(&output, va_arg(arguments, const void *));
                break;
            case 'c':
                output_char(&output, (char)va_arg(arguments, int));
                break;
            case 's':
                output_string(&output, va_arg(arguments, const char *),
                              width, left);
                break;
            default:
                output_char(&output, '%');
                output_char(&output, format[-1]);
                break;
            }
        }
    }

    terminate_output(&output);
    return output.length;
}

static uint64_t elapsed_ns(void)
{
    uint64_t now = 0U;

    if (goth_pe_time_ns(&now) != GOTH_PE_STATUS_OK) {
        return 0U;
    }
    if (!boot_clock_valid) {
        boot_ns = now;
        boot_clock_valid = true;
        return 0U;
    }
    if (now < boot_ns) {
        return 0U;
    }
    return now - boot_ns;
}

void goth_klog_init(void)
{
    size_t index;

    if (log_initialized) {
        return;
    }

    for (index = 0U; index < GOTH_KLOG_RECORDS; index++) {
        records[index].timestamp_ns = 0U;
        records[index].level = GOTH_LOG_INFO;
        records[index].message[0] = '\0';
    }
    record_next = 0U;
    record_count = 0U;
    record_total = 0U;
    boot_ns = 0U;
    boot_clock_valid = false;
    console_level = GOTH_LOG_INFO;
    log_initialized = true;

    (void)elapsed_ns();
}

bool goth_klog_is_initialized(void)
{
    return log_initialized;
}

void goth_klog_set_console_level(goth_log_level_t level)
{
    if ((unsigned int)level <= (unsigned int)GOTH_LOG_DEBUG) {
        console_level = level;
    }
}

goth_log_level_t goth_klog_console_level(void)
{
    return console_level;
}

const char *goth_klog_level_name(goth_log_level_t level)
{
    switch (level) {
    case GOTH_LOG_EMERG:
        return "emerg";
    case GOTH_LOG_ALERT:
        return "alert";
    case GOTH_LOG_CRIT:
        return "crit";
    case GOTH_LOG_ERR:
        return "err";
    case GOTH_LOG_WARNING:
        return "warning";
    case GOTH_LOG_NOTICE:
        return "notice";
    case GOTH_LOG_INFO:
        return "info";
    case GOTH_LOG_DEBUG:
        return "debug";
    default:
        return "unknown";
    }
}

static void copy_message(char *destination, size_t capacity,
                         const char *message)
{
    size_t index = 0U;

    if (capacity == 0U) {
        return;
    }
    if (message == NULL) {
        message = "";
    }
    while (message[index] != '\0' && index + 1U < capacity) {
        destination[index] = message[index];
        index++;
    }
    destination[index] = '\0';
}

void goth_klog_emit(goth_log_level_t level, const char *message)
{
    goth_log_record_t *record;

    if (!log_initialized) {
        goth_klog_init();
    }
    if ((unsigned int)level > (unsigned int)GOTH_LOG_DEBUG) {
        level = GOTH_LOG_INFO;
    }

    record = &records[record_next];
    record->timestamp_ns = elapsed_ns();
    record->level = level;
    copy_message(record->message, sizeof(record->message), message);

    record_next = (record_next + 1U) % GOTH_KLOG_RECORDS;
    if (record_count < GOTH_KLOG_RECORDS) {
        record_count++;
    }
    record_total++;

    if (level <= console_level) {
        char line[GOTH_KLOG_MESSAGE_SIZE + 64U];
        size_t length = goth_klog_line(record_count - 1U, line,
                                       sizeof(line) - 1U);

        if (length > 0U) {
            line[length++] = '\n';
            (void)goth_pe_console_write(line, length);
        }
    }
}

void goth_klog_printf(goth_log_level_t level, const char *format, ...)
{
    char message[GOTH_KLOG_MESSAGE_SIZE];
    va_list arguments;

    va_start(arguments, format);
    (void)format_message(message, sizeof(message), format, arguments);
    va_end(arguments);

    goth_klog_emit(level, message);
}

const goth_log_record_t *goth_klog_at(size_t index)
{
    size_t oldest;

    if (!log_initialized || index >= record_count) {
        return NULL;
    }
    oldest = (record_next + GOTH_KLOG_RECORDS - record_count) %
             GOTH_KLOG_RECORDS;
    return &records[(oldest + index) % GOTH_KLOG_RECORDS];
}

size_t goth_klog_count(void)
{
    return log_initialized ? record_count : 0U;
}

size_t goth_klog_capacity(void)
{
    return GOTH_KLOG_RECORDS;
}

uint64_t goth_klog_dropped(void)
{
    return log_initialized && record_total > record_count
               ? record_total - record_count
               : 0U;
}

void goth_klog_clear(void)
{
    record_next = 0U;
    record_count = 0U;
    record_total = 0U;
}

static void format_level_prefix(log_output_t *output, goth_log_level_t level)
{
    switch (level) {
    case GOTH_LOG_EMERG:
        output_text(output, "Kernel panic: ");
        break;
    case GOTH_LOG_ALERT:
        output_text(output, "ALERT: ");
        break;
    case GOTH_LOG_CRIT:
        output_text(output, "CRITICAL: ");
        break;
    case GOTH_LOG_ERR:
        output_text(output, "ERROR: ");
        break;
    case GOTH_LOG_WARNING:
        output_text(output, "WARNING: ");
        break;
    default:
        break;
    }
}

size_t goth_klog_line(size_t index, char *buffer, size_t capacity)
{
    const goth_log_record_t *record = goth_klog_at(index);
    log_output_t output;
    uint64_t seconds;
    uint64_t micros;

    if (record == NULL || buffer == NULL || capacity == 0U) {
        if (buffer != NULL && capacity > 0U) {
            buffer[0] = '\0';
        }
        return 0U;
    }

    seconds = record->timestamp_ns / UINT64_C(1000000000);
    micros = (record->timestamp_ns % UINT64_C(1000000000)) / UINT64_C(1000);

    output.buffer = buffer;
    output.capacity = capacity;
    output.length = 0U;

    output_char(&output, '[');
    output_unsigned(&output, seconds, 10U, false, 5, ' ', false, false,
                    false);
    output_char(&output, '.');
    output_unsigned(&output, micros, 10U, false, 6, '0', false, false, false);
    output_text(&output, "] ");
    format_level_prefix(&output, record->level);
    output_text(&output, record->message);
    terminate_output(&output);
    return output.length;
}

void goth_klog_register_memory(void)
{
    (void)goth_mem_register(records, sizeof(records),
                            GOTH_MEM_READ | GOTH_MEM_WRITE,
                            GOTH_MEM_KERNEL, 0U, "klog ring");
}
