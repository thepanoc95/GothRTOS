#include "lvgl.h"

int main(void)
{
    char *buffer;
    char *grown;
    int length;

    lv_init();
    if (lv_mem_test() != LV_RESULT_OK) {
        return 1;
    }

    buffer = (char *)lv_malloc(16U);
    if (buffer == NULL) {
        return 2;
    }
    grown = (char *)lv_realloc(buffer, 32U);
    if (grown == NULL) {
        lv_free(buffer);
        return 3;
    }
    buffer = grown;

    length = lv_snprintf(buffer, 32U, "LVGL %d.%d",
                         LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
    if (length != 8 || buffer[0] != 'L' || buffer[4] != ' ' ||
        buffer[5] != '9' || buffer[6] != '.' || buffer[7] != '2' ||
        buffer[8] != '\0') {
        lv_free(buffer);
        return 4;
    }
    if (lv_strncat(buffer, "!", 1U) != buffer ||
        buffer[8] != '!' || buffer[9] != '\0' ||
        lv_strncat(buffer, "ignored", 0U) != buffer) {
        lv_free(buffer);
        return 5;
    }

    lv_free(buffer);
    return 0;
}
