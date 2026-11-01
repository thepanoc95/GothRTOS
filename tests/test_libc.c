#define errno gothc_errno
#define malloc gothc_malloc
#define calloc gothc_calloc
#define realloc gothc_realloc
#define free gothc_free
#define memcpy gothc_memcpy
#define memmove gothc_memmove
#define memset gothc_memset
#define memcmp gothc_memcmp
#define strlen gothc_strlen
#define strnlen gothc_strnlen
#define strcmp gothc_strcmp
#define strncmp gothc_strncmp
#define strcpy gothc_strcpy
#define strncpy gothc_strncpy
#define strcat gothc_strcat
#define strchr gothc_strchr
#define strrchr gothc_strrchr
#define strtol gothc_strtol
#define strtoul gothc_strtoul
#define strtoll gothc_strtoll
#define strtoull gothc_strtoull
#define atoi gothc_atoi
#define atol gothc_atol
#define atoll gothc_atoll
#define abs gothc_abs
#define labs gothc_labs

#include <errno.h>
#include <gothrtos/libc.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>

void abort(void);

static unsigned char heap_storage[16U * 1024U];

static void test_memory_and_strings(void)
{
    char text[32] = "Goth";
    char banana[] = "banana";

    assert(strlen(text) == 4U);
    assert(strnlen(text, 3U) == 3U);
    assert(strcmp(text, "Goth") == 0);
    assert(strncmp(text, "GothRTOS", 4U) == 0);
    assert(strchr(text, 't') == text + 2);
    assert(strrchr(banana, 'a') == banana + 5);
    assert(memcmp("abc", "abd", 3U) < 0);

    strcat(text, "RTOS");
    assert(strcmp(text, "GothRTOS") == 0);
    memmove(text + 1, text, 7U);
    assert(memcmp(text, "GGothRT", 7U) == 0);
    memset(text, 'x', 3U);
    assert(text[0] == 'x' && text[1] == 'x' && text[2] == 'x');
}

static void test_integer_conversion(void)
{
    char *end;

    errno = 0;
    assert(strtol(" -0x2a tail", &end, 0) == -42L);
    assert(*end == ' ');
    assert(strtoul("077", &end, 0) == 63UL);
    assert(*end == '\0');
    assert(strtoll("-9223372036854775808", &end, 10) == LLONG_MIN);
    assert(*end == '\0');
    assert(strtoll("9223372036854775807", &end, 10) == LLONG_MAX);
    assert(*end == '\0');
    assert(strtoull("18446744073709551615", &end, 10) == ULLONG_MAX);
    assert(*end == '\0');
    assert(atoll("-789") == -789LL);
    assert(atoi(" 123") == 123);
    assert(atol("-456") == -456L);

    errno = 0;
    assert(strtol("999999999999999999999999", &end, 10) == LONG_MAX);
    assert(errno == ERANGE);
    assert(*end == '\0');
    errno = 0;
    assert(strtoull("18446744073709551616", &end, 10) == ULLONG_MAX);
    assert(errno == ERANGE);
    assert(*end == '\0');
    errno = 0;
    assert(strtoll("9223372036854775808", &end, 10) == LLONG_MAX);
    assert(errno == ERANGE);
    errno = 0;
    assert(strtoll("-9223372036854775809", &end, 10) == LLONG_MIN);
    assert(errno == ERANGE);

    errno = 0;
    assert(strtol("no digits", &end, 10) == 0L);
    assert(end[0] == 'n');

    errno = 0;
    assert(strtol("10", &end, 1) == 0L);
    assert(errno == EINVAL);
    assert(end[0] == '1');
}

static void test_heap(void)
{
    unsigned char *first;
    unsigned char *second;
    unsigned char *grown;
    size_t initial_available;
    size_t available_after_free;
    size_t index;
    unsigned char outside_heap;

    goth_libc_heap_reset();
    assert(malloc(1U) == NULL);
    assert(errno == ENOMEM);
    assert(goth_libc_heap_init(NULL, sizeof(heap_storage)) == -1);
    assert(errno == EINVAL);
    assert(goth_libc_heap_init(heap_storage, sizeof(heap_storage)) == 0);
    initial_available = goth_libc_heap_available();

    first = (unsigned char *)malloc(64U);
    second = (unsigned char *)calloc(8U, 8U);
    assert(first != NULL && second != NULL);
    assert((uintptr_t)first % _Alignof(max_align_t) == 0U);
    for (index = 0U; index < 64U; index++) {
        assert(second[index] == 0U);
        first[index] = (unsigned char)index;
    }

    grown = (unsigned char *)realloc(first, 256U);
    assert(grown != NULL);
    for (index = 0U; index < 64U; index++) {
        assert(grown[index] == (unsigned char)index);
    }

    free(second);
    errno = 0;
    free(&outside_heap);
    assert(errno == EINVAL);
    free(grown);
    free(NULL);
    errno = 0;
    free(first);
    assert(errno == EINVAL);
    available_after_free = goth_libc_heap_available();
    assert(available_after_free == initial_available);

    first = (unsigned char *)malloc(128U);
    assert(first != NULL);
    first[0] = 0x5AU;
    errno = 0;
    assert(realloc(first, sizeof(heap_storage) * 2U) == NULL);
    assert(errno == ENOMEM);
    assert(first[0] == 0x5AU);
    free(first);
    errno = 0;
    free(first);
    assert(errno == EINVAL);
    assert(goth_libc_heap_init(heap_storage, sizeof(heap_storage)) == 0);
    goth_libc_heap_reset();
}

int main(void)
{
    test_memory_and_strings();
    test_integer_conversion();
    test_heap();
    return 0;
}
