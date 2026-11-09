#ifndef SYSGOTH_AMD64_VGA_H
#define SYSGOTH_AMD64_VGA_H

#include <stddef.h>
#include <stdint.h>

/* VGA text mode (80x25, color) console at physical 0xB8000. The AMD64
 * multiboot entry identity-maps the first 1 GiB, so this is directly
 * accessible in long mode. */

void sg_vga_init(void);
void sg_vga_clear(void);
void sg_vga_putc(char character);
void sg_vga_write(const char *text, size_t length);
void sg_vga_puts(const char *text);
void sg_vga_set_color(uint8_t fg, uint8_t bg);

#define SG_VGA_LIGHT_GREY 7U
#define SG_VGA_BLACK 0U
#define SG_VGA_LIGHT_CYAN 11U
#define SG_VGA_LIGHT_GREEN 10U
#define SG_VGA_LIGHT_RED 12U
#define SG_VGA_YELLOW 14U

#endif
