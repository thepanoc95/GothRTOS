#include "kbarena.h"
#include "kexpr.h"
#include "kmempool.h"
#include "kstring.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void test_kstring(void)
{
    kstring_t string = {0, 0, NULL};

    assert(ksprintf(&string, "%s:%d", "goth", 42) >= 0);
    assert(strcmp(string.s, "goth:42") == 0);
    free(string.s);
}

static void test_expression(void)
{
    int error = 0;
    kexpr_t *expression = ke_parse("1 + 2 * 3", &error);

    assert(error == 0);
    assert(expression != NULL);
    assert(fabs(ke_eval_real(expression, &error) - 7.0) < 1e-12);
    assert(error == 0);
    ke_destroy(expression);
}

static void test_allocators(void)
{
    void *arena = kba_init(128U);
    void *first;
    void *second;
    void *pool;
    void *item;

    assert(arena != NULL);
    first = kba_alloc(arena, 16U, 8U);
    assert(first != NULL);
    assert(kba_save(arena) == 0);
    second = kba_alloc(arena, 24U, 8U);
    assert(second != NULL);
    assert(kba_restore(arena) == 0);
    kba_destroy(arena);

    pool = kmp_init(32U);
    assert(pool != NULL);
    item = kmp_alloc(pool);
    assert(item != NULL);
    memset(item, 0xA5, 32U);
    kmp_free(pool, item);
    assert(kmp_alloc(pool) == item);
    kmp_destroy(pool);
}

int main(void)
{
    test_kstring();
    test_expression();
    test_allocators();
    return 0;
}
