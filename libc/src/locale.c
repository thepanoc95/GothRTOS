#include <locale.h>
#include <stddef.h>

static struct lconv c_locale = {
    (char *)".", (char *)"", (char *)"", (char *)"", (char *)"",
    (char *)"", (char *)"", (char *)"", (char *)"", (char *)"",
    127, 127, 127, 127, 127, 127, 127, 127
};

char *setlocale(int category, const char *locale)
{
    (void)category;

    if (locale == NULL) {
        return (char *)"C";
    }
    if (locale[0] == 'C' && locale[1] == '\0') {
        return (char *)"C";
    }
    return NULL;
}

struct lconv *localeconv(void)
{
    return &c_locale;
}
