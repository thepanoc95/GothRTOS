#include "amd64_vga.h"

#define VGA_BASE ((volatile uint16_t *)0xB8000UL)
#define VGA_COLS 80U
#define VGA_ROWS 25U

static uint16_t vga_row;
static uint16_t vga_col;
static uint8_t vga_color;

static uint16_t vga_entry(char c)
{
    return (uint16_t)c | ((uint16_t)vga_color << 8);
}

static void vga_scroll(void)
{
    uint16_t blank = vga_entry(' ');
    uint16_t row;
    uint16_t col;

    if (vga_row < VGA_ROWS) {
        return;
    }

    for (row = 1U; row < VGA_ROWS; row++) {
        for (col = 0U; col < VGA_COLS; col++) {
            VGA_BASE[(row - 1U) * VGA_COLS + col] =
                VGA_BASE[row * VGA_COLS + col];
        }
    }
    for (col = 0U; col < VGA_COLS; col++) {
        VGA_BASE[(VGA_ROWS - 1U) * VGA_COLS + col] = blank;
    }
    vga_row = VGA_ROWS - 1U;
}

void sg_vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = (uint8_t)((bg << 4) | (fg & 0x0FU));
}

void sg_vga_clear(void)
{
    uint16_t blank = vga_entry(' ');
    uint16_t index;

    for (index = 0U; index < VGA_COLS * VGA_ROWS; index++) {
        VGA_BASE[index] = blank;
    }
    vga_row = 0U;
    vga_col = 0U;
}

void sg_vga_init(void)
{
    vga_color = (uint8_t)((SG_VGA_BLACK << 4) | SG_VGA_LIGHT_GREY);
    sg_vga_clear();
}

void sg_vga_putc(char character)
{
    if (character == '\n') {
        vga_col = 0U;
        vga_row++;
    } else if (character == '\r') {
        vga_col = 0U;
    } else if (character == '\b') {
        if (vga_col > 0U) {
            vga_col--;
            VGA_BASE[vga_row * VGA_COLS + vga_col] = vga_entry(' ');
        }
    } else if (character == '\t') {
        uint16_t next = (uint16_t)((vga_col + 8U) & ~(uint16_t)7U);

        while (vga_col < next && vga_col < VGA_COLS) {
            VGA_BASE[vga_row * VGA_COLS + vga_col] = vga_entry(' ');
            vga_col++;
        }
    } else {
        VGA_BASE[vga_row * VGA_COLS + vga_col] = vga_entry(character);
        vga_col++;
    }

    if (vga_col >= VGA_COLS) {
        vga_col = 0U;
        vga_row++;
    }
    vga_scroll();
}

void sg_vga_write(const char *text, size_t length)
{
    size_t index;

    if (text == NULL) {
        return;
    }
    for (index = 0U; index < length; index++) {
        sg_vga_putc(text[index]);
    }
}

void sg_vga_puts(const char *text)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        sg_vga_putc(*text++);
    }
}
