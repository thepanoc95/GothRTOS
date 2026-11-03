/*
 * libgothgfx -- a small framebuffer + touchscreen binding for GothRTOS.
 *
 * The library is deliberately self-contained.  It talks to the Linux
 * framebuffer (usually /dev/graphics/fb0 on Android) through the classic
 * FBIOGET_* ioctls and to evdev touchscreens (/dev/input/event*) through
 * EVIOCG* ioctls.  It can also create an in-memory surface, which keeps the
 * module testable on development machines that have no framebuffer.
 *
 * The Lua API is intentionally small and immediate-mode:
 *
 *   local gfx = require("gothgfx")
 *   local screen = assert(gfx.open{ rotate = 90 })
 *   screen:clear(0x101018)
 *   screen:fill(40, 40, 200, 80, 0x2b6cb0)
 *   screen:text(56, 64, "GothRTOS", 0xffffff, 2)
 *   for _, ev in ipairs(screen:pollEvents(16)) do ... end
 *   screen:close()
 *
 * Drawing is clipped to the logical (rotated) surface.  Colours are given as
 * 0xRRGGBB or 0xAARRGGBB and are packed to whatever the framebuffer uses.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/input.h>
#endif

#include "lua.h"
#include "lauxlib.h"

#include "gothgfx_font.h"

#define GOTHGFX_MAX_INPUTS 8
#define GOTHGFX_MAX_TOUCHES 16
#define GOTHGFX_MAX_EVENTS 128
#define GOTHGFX_INPUT_WAIT_MS 1000

typedef struct GothGfxEvent {
    int type; /* 1 = touch, 2 = key */
    int action; /* 0 = down, 1 = up, 2 = move, 3 = cancel */
    int id;
    int x;
    int y;
    int raw_x;
    int raw_y;
    int code;
    uint64_t time_ms;
} GothGfxEvent;

typedef struct GothGfxTouchSlot {
    int tracking_id;
    int active;
    int down_pending;
    int release_pending;
    int raw_x;
    int raw_y;
    int last_x;
    int last_y;
} GothGfxTouchSlot;

typedef struct GothGfxInput {
    int fd;
    int open;
    char path[256];
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    int has_mt;
    int has_mt_slots;
    int has_btn_touch;
    int current_slot;
    GothGfxTouchSlot slots[GOTHGFX_MAX_TOUCHES];

    /* Protocol A / single-touch accumulation. */
    int pending_count;
    int pending_raw_x[GOTHGFX_MAX_TOUCHES];
    int pending_raw_y[GOTHGFX_MAX_TOUCHES];
    int previous_count;
    int previous_raw_x[GOTHGFX_MAX_TOUCHES];
    int previous_raw_y[GOTHGFX_MAX_TOUCHES];

    int single_active;
    int single_down_pending;
    int single_up_pending;
    int single_raw_x;
    int single_raw_y;
    int single_last_x;
    int single_last_y;
} GothGfxInput;

typedef struct GothGfxDisplay {
    int fd;
    int open;
    unsigned char *memory;
    size_t memory_size;
    int owns_memory;
    int width;
    int height;
    int logical_width;
    int logical_height;
    int bpp;
    int bytes_per_pixel;
    int stride;
    int rotation;
    int double_buffer;
    int swap_xy;
    int invert_x;
    int invert_y;
    /* Optional override of the evdev axis range (device profiles). */
    int touch_x_min;
    int touch_x_max;
    int touch_y_min;
    int touch_y_max;
    int touch_x_range;
    int touch_y_range;
    uint32_t draw_offset;
    uint32_t pan_offset;
    uint32_t red_offset;
    uint32_t red_length;
    uint32_t green_offset;
    uint32_t green_length;
    uint32_t blue_offset;
    uint32_t blue_length;
    uint32_t alpha_offset;
    uint32_t alpha_length;
    int grayscale;

    GothGfxInput inputs[GOTHGFX_MAX_INPUTS];
    int input_count;

    GothGfxEvent queue[GOTHGFX_MAX_EVENTS];
    int queue_head;
    int queue_count;
} GothGfxDisplay;

/* -------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* -------------------------------------------------------------------------- */

static uint64_t gothgfx_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000) +
           (uint64_t)(ts.tv_nsec / UINT64_C(1000000));
}

static const char *gothgfx_errno_text(int error)
{
    switch (error) {
    case 0:
        return "ok";
    case EACCES:
        return "permission denied";
    case ENOENT:
        return "no such file or directory";
    case ENODEV:
        return "no such device";
    case EBUSY:
        return "device busy";
    case EINVAL:
        return "invalid argument";
    case ENOMEM:
        return "out of memory";
    case ENOSYS:
        return "operation not supported";
    default:
        return strerror(error);
    }
}

static int gothgfx_get_integer(lua_State *L, int table, const char *name,
                               lua_Integer fallback, lua_Integer *out)
{
    if (!lua_istable(L, table)) {
        *out = fallback;
        return 1;
    }
    lua_getfield(L, table, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        *out = fallback;
        return 1;
    }
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    *out = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return 1;
}

static int gothgfx_get_boolean(lua_State *L, int table, const char *name,
                               int fallback)
{
    int result;

    if (!lua_istable(L, table)) {
        return fallback;
    }
    lua_getfield(L, table, name);
    result = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result != 0;
}

static const char *gothgfx_get_string(lua_State *L, int table, const char *name)
{
    const char *result = NULL;

    if (!lua_istable(L, table)) {
        return NULL;
    }
    lua_getfield(L, table, name);
    if (lua_isstring(L, -1)) {
        result = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    return result;
}

static unsigned int gothgfx_component_mask(uint32_t length)
{
    if (length == 0U) {
        return 0U;
    }
    if (length >= 32U) {
        return UINT32_MAX;
    }
    return (1U << length) - 1U;
}

static uint32_t gothgfx_scale_component(unsigned int value, uint32_t length)
{
    if (length == 0U) {
        return 0U;
    }
    if (length >= 8U) {
        return (uint32_t)value << (length - 8U);
    }
    return (uint32_t)value >> (8U - length);
}

static uint32_t gothgfx_pack_color(const GothGfxDisplay *display,
                                   lua_Integer color)
{
    uint32_t rgb = (uint32_t)color;
    unsigned int red = (rgb >> 16) & 0xffU;
    unsigned int green = (rgb >> 8) & 0xffU;
    unsigned int blue = rgb & 0xffU;
    uint32_t packed = 0U;

    if (display->grayscale) {
        return ((red * 77U + green * 150U + blue * 29U) >> 8) & 0xffU;
    }

    packed |= (gothgfx_scale_component(red, display->red_length) &
               gothgfx_component_mask(display->red_length))
              << display->red_offset;
    packed |= (gothgfx_scale_component(green, display->green_length) &
               gothgfx_component_mask(display->green_length))
              << display->green_offset;
    packed |= (gothgfx_scale_component(blue, display->blue_length) &
               gothgfx_component_mask(display->blue_length))
              << display->blue_offset;
    if (display->alpha_length != 0U) {
        packed |= (gothgfx_scale_component(0xffU, display->alpha_length) &
                   gothgfx_component_mask(display->alpha_length))
                  << display->alpha_offset;
    }
    return packed;
}

static uint32_t gothgfx_load_pixel(const GothGfxDisplay *display,
                                  size_t offset)
{
    const unsigned char *pixel;
    uint32_t packed = 0U;

    if (offset > display->memory_size ||
        display->memory_size - offset < (size_t)display->bytes_per_pixel) {
        return 0U;
    }
    pixel = display->memory + offset;
    switch (display->bytes_per_pixel) {
    case 1:
        packed = pixel[0];
        break;
    case 2:
        packed = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8);
        break;
    case 3:
        packed = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) |
                 ((uint32_t)pixel[2] << 16);
        break;
    case 4:
        packed = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) |
                 ((uint32_t)pixel[2] << 16) | ((uint32_t)pixel[3] << 24);
        break;
    default:
        break;
    }
    return packed;
}

static uint32_t gothgfx_unpack_component(uint32_t packed, uint32_t offset,
                                         uint32_t length)
{
    uint32_t value;

    if (length == 0U) {
        return 0U;
    }
    value = (packed >> offset) & gothgfx_component_mask(length);
    if (length >= 8U) {
        return value >> (length - 8U);
    }
    return value << (8U - length);
}

static uint32_t gothgfx_unpack_color(const GothGfxDisplay *display,
                                     uint32_t packed)
{
    if (display->grayscale) {
        uint32_t gray = packed & 0xffU;

        return (gray << 16) | (gray << 8) | gray;
    }
    return (gothgfx_unpack_component(packed, display->red_offset,
                                     display->red_length)
            << 16) |
           (gothgfx_unpack_component(packed, display->green_offset,
                                     display->green_length)
            << 8) |
           gothgfx_unpack_component(packed, display->blue_offset,
                                    display->blue_length);
}

static void gothgfx_store_pixel(GothGfxDisplay *display, size_t offset,
                                uint32_t packed)
{
    unsigned char *pixel;

    if (offset > display->memory_size ||
        display->memory_size - offset < (size_t)display->bytes_per_pixel) {
        return;
    }
    pixel = display->memory + offset;
    switch (display->bytes_per_pixel) {
    case 1:
        pixel[0] = (unsigned char)packed;
        break;
    case 2:
        pixel[0] = (unsigned char)(packed & 0xffU);
        pixel[1] = (unsigned char)((packed >> 8) & 0xffU);
        break;
    case 3:
        pixel[0] = (unsigned char)(packed & 0xffU);
        pixel[1] = (unsigned char)((packed >> 8) & 0xffU);
        pixel[2] = (unsigned char)((packed >> 16) & 0xffU);
        break;
    case 4:
        pixel[0] = (unsigned char)(packed & 0xffU);
        pixel[1] = (unsigned char)((packed >> 8) & 0xffU);
        pixel[2] = (unsigned char)((packed >> 16) & 0xffU);
        pixel[3] = (unsigned char)((packed >> 24) & 0xffU);
        break;
    default:
        break;
    }
}

static int gothgfx_map_point(const GothGfxDisplay *display, int x, int y,
                             int *native_x, int *native_y)
{
    if (x < 0 || y < 0 || x >= display->logical_width ||
        y >= display->logical_height) {
        return 0;
    }
    switch (display->rotation) {
    case 90:
        *native_x = display->width - 1 - y;
        *native_y = x;
        break;
    case 180:
        *native_x = display->width - 1 - x;
        *native_y = display->height - 1 - y;
        break;
    case 270:
        *native_x = y;
        *native_y = display->height - 1 - x;
        break;
    default:
        *native_x = x;
        *native_y = y;
        break;
    }
    return 1;
}

static void gothgfx_put_pixel(GothGfxDisplay *display, int x, int y,
                              uint32_t packed)
{
    int native_x;
    int native_y;
    size_t offset;

    if (!gothgfx_map_point(display, x, y, &native_x, &native_y)) {
        return;
    }
    offset = display->draw_offset +
             (size_t)native_y * (size_t)display->stride +
             (size_t)native_x * (size_t)display->bytes_per_pixel;
    gothgfx_store_pixel(display, offset, packed);
}

static void gothgfx_fill_rect(GothGfxDisplay *display, int x, int y, int width,
                              int height, uint32_t packed)
{
    int row;
    int column;

    if (width <= 0 || height <= 0) {
        return;
    }
    /* Fast path for an unrotated, byte-aligned surface. */
    if (display->rotation == 0 && x >= 0 && y >= 0 &&
        x + width <= display->width && y + height <= display->height &&
        (display->bytes_per_pixel == 2 || display->bytes_per_pixel == 4)) {
        for (row = y; row < y + height; row++) {
            unsigned char *line = display->memory + display->draw_offset +
                                  (size_t)row * (size_t)display->stride +
                                  (size_t)x * (size_t)display->bytes_per_pixel;
            if (display->bytes_per_pixel == 4) {
                uint32_t *pixels = (uint32_t *)line;
                for (column = 0; column < width; column++) {
                    pixels[column] = packed;
                }
            } else {
                uint16_t *pixels = (uint16_t *)line;
                for (column = 0; column < width; column++) {
                    pixels[column] = (uint16_t)packed;
                }
            }
        }
        return;
    }

    for (row = y; row < y + height; row++) {
        for (column = x; column < x + width; column++) {
            gothgfx_put_pixel(display, column, row, packed);
        }
    }
}

static void gothgfx_draw_rect(GothGfxDisplay *display, int x, int y, int width,
                              int height, int thickness, uint32_t packed)
{
    if (width <= 0 || height <= 0 || thickness <= 0) {
        return;
    }
    if (thickness * 2 >= width || thickness * 2 >= height) {
        gothgfx_fill_rect(display, x, y, width, height, packed);
        return;
    }
    gothgfx_fill_rect(display, x, y, width, thickness, packed);
    gothgfx_fill_rect(display, x, y + height - thickness, width, thickness,
                      packed);
    gothgfx_fill_rect(display, x, y + thickness, thickness,
                      height - thickness * 2, packed);
    gothgfx_fill_rect(display, x + width - thickness, y + thickness, thickness,
                      height - thickness * 2, packed);
}

static void gothgfx_draw_line(GothGfxDisplay *display, int x0, int y0, int x1,
                              int y1, uint32_t packed)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        gothgfx_put_pixel(display, x0, y0, packed);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (error * 2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (error * 2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void gothgfx_draw_char(GothGfxDisplay *display, int x, int y,
                              unsigned char character, int scale,
                              uint32_t packed)
{
    int glyph_index;
    int row;
    int column;

    if (character < GOTHGFX_FONT_FIRST || character > GOTHGFX_FONT_LAST) {
        character = '?';
    }
    glyph_index = (int)character - GOTHGFX_FONT_FIRST;
    for (row = 0; row < GOTHGFX_FONT_HEIGHT; row++) {
        unsigned char bits = gothgfx_font[glyph_index][row];
        for (column = 0; column < GOTHGFX_FONT_WIDTH; column++) {
            if ((bits & (0x80U >> column)) != 0U) {
                if (scale == 1) {
                    gothgfx_put_pixel(display, x + column, y + row, packed);
                } else {
                    gothgfx_fill_rect(display, x + column * scale,
                                      y + row * scale, scale, scale, packed);
                }
            }
        }
    }
}

static int gothgfx_draw_text(GothGfxDisplay *display, int x, int y,
                             const char *text, int scale, uint32_t packed)
{
    int cursor = x;
    int advance;

    if (scale < 1) {
        scale = 1;
    }
    advance = GOTHGFX_FONT_WIDTH * scale;
    while (*text != '\0') {
        unsigned char character = (unsigned char)*text++;
        if (character == '\n') {
            x = cursor;
            y += GOTHGFX_FONT_HEIGHT * scale;
            continue;
        }
        gothgfx_draw_char(display, cursor, y, character, scale, packed);
        cursor += advance;
    }
    return cursor - x;
}

/* -------------------------------------------------------------------------- */
/* Event queue                                                                */
/* -------------------------------------------------------------------------- */

static void gothgfx_queue_event(GothGfxDisplay *display,
                                const GothGfxEvent *event)
{
    int index;

    if (display->queue_count >= GOTHGFX_MAX_EVENTS) {
        return;
    }
    index = (display->queue_head + display->queue_count) % GOTHGFX_MAX_EVENTS;
    display->queue[index] = *event;
    display->queue_count++;
}

static int gothgfx_dequeue_event(GothGfxDisplay *display, GothGfxEvent *event)
{
    if (display->queue_count == 0) {
        return 0;
    }
    *event = display->queue[display->queue_head];
    display->queue_head = (display->queue_head + 1) % GOTHGFX_MAX_EVENTS;
    display->queue_count--;
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Input handling                                                             */
/* -------------------------------------------------------------------------- */

static int gothgfx_abs_has(const unsigned char *bits, int code)
{
    return (bits[code / 8] & (1U << (code % 8))) != 0U;
}

static int gothgfx_scale_axis(int raw, int minimum, int maximum, int size)
{
    long long scaled;

    if (maximum <= minimum || size <= 1) {
        return 0;
    }
    scaled = (long long)(raw - minimum) * (long long)(size - 1) /
             (long long)(maximum - minimum);
    if (scaled < 0) {
        scaled = 0;
    }
    if (scaled >= size) {
        scaled = size - 1;
    }
    return (int)scaled;
}

static void gothgfx_touch_coordinates(const GothGfxDisplay *display,
                                      const GothGfxInput *input, int raw_x,
                                      int raw_y, int *x, int *y)
{
    int x_min = display->touch_x_range ? display->touch_x_min : input->x_min;
    int x_max = display->touch_x_range ? display->touch_x_max : input->x_max;
    int y_min = display->touch_y_range ? display->touch_y_min : input->y_min;
    int y_max = display->touch_y_range ? display->touch_y_max : input->y_max;
    int native_x = gothgfx_scale_axis(raw_x, x_min, x_max, display->width);
    int native_y = gothgfx_scale_axis(raw_y, y_min, y_max, display->height);
    int logical_x;
    int logical_y;

    if (display->swap_xy) {
        int temporary = native_x;
        native_x = native_y;
        native_y = temporary;
    }
    if (display->invert_x) {
        native_x = display->width - 1 - native_x;
    }
    if (display->invert_y) {
        native_y = display->height - 1 - native_y;
    }

    switch (display->rotation) {
    case 90:
        logical_x = native_y;
        logical_y = display->width - 1 - native_x;
        break;
    case 180:
        logical_x = display->width - 1 - native_x;
        logical_y = display->height - 1 - native_y;
        break;
    case 270:
        logical_x = display->height - 1 - native_y;
        logical_y = native_x;
        break;
    default:
        logical_x = native_x;
        logical_y = native_y;
        break;
    }
    if (logical_x < 0) {
        logical_x = 0;
    }
    if (logical_y < 0) {
        logical_y = 0;
    }
    if (logical_x >= display->logical_width) {
        logical_x = display->logical_width - 1;
    }
    if (logical_y >= display->logical_height) {
        logical_y = display->logical_height - 1;
    }
    *x = logical_x;
    *y = logical_y;
}

static void gothgfx_emit_touch(const GothGfxDisplay *display,
                               const GothGfxInput *input, GothGfxEvent *event,
                               int action, int id, int raw_x, int raw_y,
                               uint64_t time_ms)
{
    event->type = 1;
    event->action = action;
    event->id = id;
    event->raw_x = raw_x;
    event->raw_y = raw_y;
    gothgfx_touch_coordinates(display, input, raw_x, raw_y, &event->x,
                              &event->y);
    event->code = 0;
    event->time_ms = time_ms;
}

static void gothgfx_emit_key(GothGfxEvent *event, int action, int code,
                             uint64_t time_ms)
{
    event->type = 2;
    event->action = action;
    event->id = 0;
    event->x = 0;
    event->y = 0;
    event->raw_x = 0;
    event->raw_y = 0;
    event->code = code;
    event->time_ms = time_ms;
}

static void gothgfx_commit_protocol_b(GothGfxDisplay *display,
                                      GothGfxInput *input, uint64_t time_ms)
{
    int index;

    for (index = 0; index < GOTHGFX_MAX_TOUCHES; index++) {
        GothGfxTouchSlot *slot = &input->slots[index];
        GothGfxEvent event;

        if (slot->release_pending) {
            if (slot->active) {
                gothgfx_emit_touch(display, input, &event, 1, index,
                                   slot->raw_x, slot->raw_y, time_ms);
                gothgfx_queue_event(display, &event);
            }
            slot->active = 0;
            slot->down_pending = 0;
            slot->release_pending = 0;
            slot->tracking_id = -1;
            continue;
        }
        if (!slot->active) {
            continue;
        }
        if (slot->down_pending) {
            gothgfx_emit_touch(display, input, &event, 0, index, slot->raw_x,
                               slot->raw_y, time_ms);
            gothgfx_queue_event(display, &event);
            slot->down_pending = 0;
            slot->last_x = slot->raw_x;
            slot->last_y = slot->raw_y;
        } else if (slot->raw_x != slot->last_x || slot->raw_y != slot->last_y) {
            gothgfx_emit_touch(display, input, &event, 2, index, slot->raw_x,
                               slot->raw_y, time_ms);
            gothgfx_queue_event(display, &event);
            slot->last_x = slot->raw_x;
            slot->last_y = slot->raw_y;
        }
    }
}

static void gothgfx_commit_protocol_a(GothGfxDisplay *display,
                                      GothGfxInput *input, uint64_t time_ms)
{
    int index;

    for (index = 0; index < input->pending_count; index++) {
        GothGfxEvent event;

        if (index >= input->previous_count) {
            gothgfx_emit_touch(display, input, &event, 0, index,
                               input->pending_raw_x[index],
                               input->pending_raw_y[index], time_ms);
            gothgfx_queue_event(display, &event);
        } else if (input->pending_raw_x[index] !=
                       input->previous_raw_x[index] ||
                   input->pending_raw_y[index] !=
                       input->previous_raw_y[index]) {
            gothgfx_emit_touch(display, input, &event, 2, index,
                               input->pending_raw_x[index],
                               input->pending_raw_y[index], time_ms);
            gothgfx_queue_event(display, &event);
        }
    }
    for (index = input->pending_count; index < input->previous_count; index++) {
        GothGfxEvent event;

        gothgfx_emit_touch(display, input, &event, 1, index,
                           input->previous_raw_x[index],
                           input->previous_raw_y[index], time_ms);
        gothgfx_queue_event(display, &event);
    }

    input->previous_count = input->pending_count;
    for (index = 0; index < input->pending_count; index++) {
        input->previous_raw_x[index] = input->pending_raw_x[index];
        input->previous_raw_y[index] = input->pending_raw_y[index];
    }
    input->pending_count = 0;
}

static void gothgfx_commit_single(GothGfxDisplay *display, GothGfxInput *input,
                                  uint64_t time_ms)
{
    GothGfxEvent event;

    if (input->single_up_pending && input->single_active) {
        gothgfx_emit_touch(display, input, &event, 1, 0, input->single_raw_x,
                           input->single_raw_y, time_ms);
        gothgfx_queue_event(display, &event);
        input->single_active = 0;
    } else if (input->single_down_pending) {
        gothgfx_emit_touch(display, input, &event, 0, 0, input->single_raw_x,
                           input->single_raw_y, time_ms);
        gothgfx_queue_event(display, &event);
        input->single_active = 1;
        input->single_last_x = input->single_raw_x;
        input->single_last_y = input->single_raw_y;
    } else if (input->single_active &&
               (input->single_raw_x != input->single_last_x ||
                input->single_raw_y != input->single_last_y)) {
        gothgfx_emit_touch(display, input, &event, 2, 0, input->single_raw_x,
                           input->single_raw_y, time_ms);
        gothgfx_queue_event(display, &event);
        input->single_last_x = input->single_raw_x;
        input->single_last_y = input->single_raw_y;
    }
    input->single_down_pending = 0;
    input->single_up_pending = 0;
}

static void gothgfx_input_reset(GothGfxInput *input)
{
    int index;

    for (index = 0; index < GOTHGFX_MAX_TOUCHES; index++) {
        input->slots[index].tracking_id = -1;
        input->slots[index].active = 0;
        input->slots[index].down_pending = 0;
        input->slots[index].release_pending = 0;
    }
    input->pending_count = 0;
    input->previous_count = 0;
    input->single_active = 0;
    input->single_down_pending = 0;
    input->single_up_pending = 0;
}

static void gothgfx_handle_input_event(GothGfxDisplay *display,
                                       GothGfxInput *input,
                                       const struct input_event *event)
{
    uint64_t time_ms = (uint64_t)event->time.tv_sec * UINT64_C(1000) +
                       (uint64_t)(event->time.tv_usec / 1000);
    GothGfxEvent output;

    switch (event->type) {
    case EV_SYN:
        if (event->code == SYN_DROPPED) {
            gothgfx_input_reset(input);
        } else if (event->code == SYN_MT_REPORT) {
            if (input->pending_count < GOTHGFX_MAX_TOUCHES) {
                input->pending_raw_x[input->pending_count] = input->single_raw_x;
                input->pending_raw_y[input->pending_count] = input->single_raw_y;
                input->pending_count++;
            }
        } else if (event->code == SYN_REPORT) {
            if (input->has_mt_slots) {
                gothgfx_commit_protocol_b(display, input, time_ms);
            } else if (input->has_mt) {
                gothgfx_commit_protocol_a(display, input, time_ms);
            } else {
                gothgfx_commit_single(display, input, time_ms);
            }
        }
        break;

    case EV_ABS:
        switch (event->code) {
        case ABS_MT_SLOT:
            if (event->value >= 0 && event->value < GOTHGFX_MAX_TOUCHES) {
                input->current_slot = event->value;
            }
            break;
        case ABS_MT_TRACKING_ID:
            if (input->current_slot >= 0 &&
                input->current_slot < GOTHGFX_MAX_TOUCHES) {
                GothGfxTouchSlot *slot = &input->slots[input->current_slot];
                if (event->value < 0) {
                    slot->release_pending = 1;
                } else {
                    if (slot->tracking_id != event->value) {
                        slot->tracking_id = event->value;
                        slot->raw_x = input->single_raw_x;
                        slot->raw_y = input->single_raw_y;
                    }
                    slot->active = 1;
                    slot->down_pending = 1;
                }
            }
            break;
        case ABS_MT_POSITION_X:
            input->single_raw_x = event->value;
            if (input->current_slot >= 0 &&
                input->current_slot < GOTHGFX_MAX_TOUCHES) {
                input->slots[input->current_slot].raw_x = event->value;
            }
            break;
        case ABS_MT_POSITION_Y:
            input->single_raw_y = event->value;
            if (input->current_slot >= 0 &&
                input->current_slot < GOTHGFX_MAX_TOUCHES) {
                input->slots[input->current_slot].raw_y = event->value;
            }
            break;
        case ABS_X:
            input->single_raw_x = event->value;
            break;
        case ABS_Y:
            input->single_raw_y = event->value;
            break;
        default:
            break;
        }
        break;

    case EV_KEY:
        if (event->code == BTN_TOUCH) {
            if (event->value != 0) {
                input->single_down_pending = 1;
            } else {
                input->single_up_pending = 1;
            }
        } else if (event->code != BTN_TOOL_FINGER &&
                   event->code != BTN_TOOL_DOUBLETAP &&
                   event->code != BTN_TOOL_TRIPLETAP &&
                   event->code != BTN_TOOL_QUADTAP &&
                   event->code != BTN_TOOL_QUINTTAP) {
            if (event->value == 0) {
                gothgfx_emit_key(&output, 1, event->code, time_ms);
                gothgfx_queue_event(display, &output);
            } else if (event->value == 1) {
                gothgfx_emit_key(&output, 0, event->code, time_ms);
                gothgfx_queue_event(display, &output);
            } else if (event->value == 2) {
                gothgfx_emit_key(&output, 2, event->code, time_ms);
                gothgfx_queue_event(display, &output);
            }
        }
        break;

    default:
        break;
    }
}

static void gothgfx_read_input(GothGfxDisplay *display, GothGfxInput *input)
{
#if defined(__linux__)
    struct input_event events[32];
    ssize_t count;

    for (;;) {
        int index;

        count = read(input->fd, events, sizeof(events));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (count == 0) {
            break;
        }
        for (index = 0; index < (int)(count / (ssize_t)sizeof(events[0]));
             index++) {
            gothgfx_handle_input_event(display, input, &events[index]);
        }
        if ((size_t)count < sizeof(events)) {
            break;
        }
    }
#else
    (void)display;
    (void)input;
#endif
}

static int gothgfx_poll_inputs(GothGfxDisplay *display, int timeout_ms)
{
#if defined(__linux__)
    struct pollfd descriptors[GOTHGFX_MAX_INPUTS];
    int index;
    int ready;

    if (display->input_count == 0) {
#if defined(__linux__)
        if (timeout_ms > 0) {
            struct timespec delay;

            delay.tv_sec = timeout_ms / 1000;
            delay.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
            while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
            }
        }
#endif
        return 0;
    }
    for (index = 0; index < display->input_count; index++) {
        descriptors[index].fd = display->inputs[index].fd;
        descriptors[index].events = POLLIN;
        descriptors[index].revents = 0;
    }
    do {
        ready = poll(descriptors, (nfds_t)display->input_count, timeout_ms);
    } while (ready < 0 && errno == EINTR);

    if (ready <= 0) {
        return 0;
    }
    for (index = 0; index < display->input_count; index++) {
        if ((descriptors[index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            gothgfx_read_input(display, &display->inputs[index]);
        }
    }
    return 1;
#else
    (void)display;
    (void)timeout_ms;
    return 0;
#endif
}

static int gothgfx_has_abs_capability(int fd, int code)
{
#if defined(__linux__)
    unsigned char bits[(ABS_MAX + 8) / 8];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) {
        return 0;
    }
    return gothgfx_abs_has(bits, code);
#else
    (void)fd;
    (void)code;
    return 0;
#endif
}

static int gothgfx_has_key_capability(int fd, int code)
{
#if defined(__linux__)
    unsigned char bits[(KEY_MAX + 8) / 8];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
        return 0;
    }
    return gothgfx_abs_has(bits, code);
#else
    (void)fd;
    (void)code;
    return 0;
#endif
}

static int gothgfx_input_open(GothGfxInput *input, const char *path)
{
#if defined(__linux__)
    struct input_absinfo info;
    int fd;

    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    memset(input, 0, sizeof(*input));
    input->fd = fd;
    input->open = 1;
    input->current_slot = 0;
    if (strlen(path) >= sizeof(input->path)) {
        close(fd);
        input->open = 0;
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(input->path, path);
    gothgfx_input_reset(input);

    input->has_mt = gothgfx_has_abs_capability(fd, ABS_MT_POSITION_X) &&
                    gothgfx_has_abs_capability(fd, ABS_MT_POSITION_Y);
    input->has_mt_slots = input->has_mt &&
                          gothgfx_has_abs_capability(fd, ABS_MT_SLOT);
    input->has_btn_touch = gothgfx_has_key_capability(fd, BTN_TOUCH);

    if (input->has_mt) {
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &info) == 0) {
            input->x_min = info.minimum;
            input->x_max = info.maximum;
        } else {
            input->x_min = 0;
            input->x_max = 4095;
        }
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &info) == 0) {
            input->y_min = info.minimum;
            input->y_max = info.maximum;
        } else {
            input->y_min = 0;
            input->y_max = 4095;
        }
    } else {
        if (ioctl(fd, EVIOCGABS(ABS_X), &info) == 0) {
            input->x_min = info.minimum;
            input->x_max = info.maximum;
        } else {
            input->x_min = 0;
            input->x_max = 4095;
        }
        if (ioctl(fd, EVIOCGABS(ABS_Y), &info) == 0) {
            input->y_min = info.minimum;
            input->y_max = info.maximum;
        } else {
            input->y_min = 0;
            input->y_max = 4095;
        }
    }
    return 0;
#else
    (void)input;
    (void)path;
    errno = ENOSYS;
    return -1;
#endif
}

static void gothgfx_input_close(GothGfxInput *input)
{
#if defined(__linux__)
    if (input->open && input->fd >= 0) {
        close(input->fd);
    }
#endif
    input->open = 0;
    input->fd = -1;
}

static int gothgfx_looks_like_touchscreen(const char *path)
{
#if defined(__linux__)
    int fd;
    int result;

    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    result = (gothgfx_has_abs_capability(fd, ABS_MT_POSITION_X) &&
              gothgfx_has_abs_capability(fd, ABS_MT_POSITION_Y)) ||
             (gothgfx_has_abs_capability(fd, ABS_X) &&
              gothgfx_has_abs_capability(fd, ABS_Y) &&
              gothgfx_has_key_capability(fd, BTN_TOUCH));
    close(fd);
    return result;
#else
    (void)path;
    return 0;
#endif
}

static int gothgfx_add_input_path(GothGfxDisplay *display, const char *path)
{
    int index;

    if (display->input_count >= GOTHGFX_MAX_INPUTS) {
        return 0;
    }
    for (index = 0; index < display->input_count; index++) {
        if (strcmp(display->inputs[index].path, path) == 0) {
            return 1;
        }
    }
    if (gothgfx_input_open(&display->inputs[display->input_count], path) != 0) {
        return 0;
    }
    display->input_count++;
    return 1;
}

static void gothgfx_add_auto_inputs(GothGfxDisplay *display)
{
#if defined(__linux__)
    DIR *directory;
    struct dirent *entry;
    char path[300];

    directory = opendir("/dev/input");
    if (directory == NULL) {
        return;
    }
    while ((entry = readdir(directory)) != NULL &&
           display->input_count < GOTHGFX_MAX_INPUTS) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        if (snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name) >=
            (int)sizeof(path)) {
            continue;
        }
        if (gothgfx_looks_like_touchscreen(path)) {
            (void)gothgfx_add_input_path(display, path);
        }
    }
    closedir(directory);
#else
    (void)display;
#endif
}

/* -------------------------------------------------------------------------- */
/* Framebuffer handling                                                       */
/* -------------------------------------------------------------------------- */

static void gothgfx_default_format(GothGfxDisplay *display)
{
    display->grayscale = 0;
    switch (display->bpp) {
    case 32:
        display->red_offset = 16;
        display->red_length = 8;
        display->green_offset = 8;
        display->green_length = 8;
        display->blue_offset = 0;
        display->blue_length = 8;
        display->alpha_offset = 24;
        display->alpha_length = 8;
        break;
    case 24:
        display->red_offset = 16;
        display->red_length = 8;
        display->green_offset = 8;
        display->green_length = 8;
        display->blue_offset = 0;
        display->blue_length = 8;
        display->alpha_offset = 0;
        display->alpha_length = 0;
        break;
    case 16:
        display->red_offset = 11;
        display->red_length = 5;
        display->green_offset = 5;
        display->green_length = 6;
        display->blue_offset = 0;
        display->blue_length = 5;
        display->alpha_offset = 0;
        display->alpha_length = 0;
        break;
    case 15:
        display->red_offset = 10;
        display->red_length = 5;
        display->green_offset = 5;
        display->green_length = 5;
        display->blue_offset = 0;
        display->blue_length = 5;
        display->alpha_offset = 0;
        display->alpha_length = 0;
        break;
    case 8:
        display->red_offset = 0;
        display->red_length = 8;
        display->green_offset = 0;
        display->green_length = 8;
        display->blue_offset = 0;
        display->blue_length = 8;
        display->alpha_offset = 0;
        display->alpha_length = 0;
        display->grayscale = 1;
        break;
    default:
        display->bpp = 32;
        display->red_offset = 16;
        display->red_length = 8;
        display->green_offset = 8;
        display->green_length = 8;
        display->blue_offset = 0;
        display->blue_length = 8;
        display->alpha_offset = 24;
        display->alpha_length = 8;
        break;
    }
}

static int gothgfx_open_memory(GothGfxDisplay *display, int width, int height,
                               int bpp)
{
    if (width <= 0 || height <= 0) {
        errno = EINVAL;
        return -1;
    }
    if (bpp <= 0) {
        bpp = 32;
    }
    if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) {
        errno = EINVAL;
        return -1;
    }
    display->width = width;
    display->height = height;
    display->bpp = bpp;
    display->bytes_per_pixel = bpp / 8;
    display->stride = width * display->bytes_per_pixel;
    display->memory_size = (size_t)display->stride * (size_t)height;
    display->memory = (unsigned char *)calloc(1, display->memory_size);
    if (display->memory == NULL) {
        errno = ENOMEM;
        return -1;
    }
    display->owns_memory = 1;
    display->fd = -1;
    display->double_buffer = 0;
    display->draw_offset = 0;
    display->pan_offset = 0;
    gothgfx_default_format(display);
    return 0;
}

/*
 * Pan the hardware scanout to `offset` bytes into the mapping.
 *
 * Android's framebuffer drivers (mdss_fb on msm8x16/8916, for example) keep
 * scanning out whichever buffer SurfaceFlinger last flipped to.  After
 * "/system/sbin/stop" the panel can therefore still be reading from a buffer
 * that is no longer mapped to us, and writes at offset 0 never appear.  An
 * explicit FBIOPAN_DISPLAY back to the buffer we draw into fixes that.
 */
static void gothgfx_pan_framebuffer(GothGfxDisplay *display, uint32_t offset)
{
#if defined(__linux__)
    struct fb_var_screeninfo variable;

    if (display->fd < 0) {
        return;
    }
    if (ioctl(display->fd, FBIOGET_VSCREENINFO, &variable) != 0) {
        return;
    }
    variable.xoffset = 0;
    variable.yoffset =
        offset / (uint32_t)(display->stride > 0 ? display->stride : 1);
    variable.activate = FB_ACTIVATE_NOW;
    (void)ioctl(display->fd, FBIOPAN_DISPLAY, &variable);
#else
    (void)display;
    (void)offset;
#endif
}

/*
 * Stopping Android can leave the panel blanked.  Unblank it through both the
 * framebuffer ioctl and the sysfs attribute (some msm kernels only honour the
 * latter for the primary panel).
 */
static void gothgfx_unblank_framebuffer(GothGfxDisplay *display,
                                        const char *path)
{
#if defined(__linux__)
    char sysfs[PATH_MAX];
    const char *name;
    int fd;

    if (display->fd < 0) {
        return;
    }
    (void)ioctl(display->fd, FBIOBLANK, FB_BLANK_UNBLANK);

    name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    if (name[0] == '\0') {
        return;
    }
    if (snprintf(sysfs, sizeof(sysfs), "/sys/class/graphics/%s/blank",
                 name) >= (int)sizeof(sysfs)) {
        return;
    }
    fd = open(sysfs, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t written = write(fd, "0", 1);
        (void)written;
        close(fd);
    }
#else
    (void)display;
    (void)path;
#endif
}

static int gothgfx_open_framebuffer(GothGfxDisplay *display, const char *path,
                                    int double_buffer)
{
#if defined(__linux__)
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    size_t map_size;

    display->fd = open(path, O_RDWR | O_CLOEXEC);
    if (display->fd < 0) {
        return -1;
    }
    memset(&fixed, 0, sizeof(fixed));
    memset(&variable, 0, sizeof(variable));
    if (ioctl(display->fd, FBIOGET_FSCREENINFO, &fixed) < 0 ||
        ioctl(display->fd, FBIOGET_VSCREENINFO, &variable) < 0) {
        int saved = errno;

        close(display->fd);
        display->fd = -1;
        errno = saved;
        return -1;
    }
    if (variable.xres == 0 || variable.yres == 0 ||
        variable.bits_per_pixel < 8) {
        close(display->fd);
        display->fd = -1;
        errno = EINVAL;
        return -1;
    }

    display->width = (int)variable.xres;
    display->height = (int)variable.yres;
    display->bpp = (int)variable.bits_per_pixel;
    display->bytes_per_pixel = display->bpp / 8;
    display->stride = (int)fixed.line_length;
    if (display->stride <= 0) {
        display->stride = display->width * display->bytes_per_pixel;
    }

    display->double_buffer = double_buffer &&
                             variable.yres_virtual >= variable.yres * 2U;
    map_size = (size_t)display->stride * (size_t)display->height;
    if (display->double_buffer) {
        map_size *= 2U;
    }
    if (fixed.smem_len > map_size) {
        map_size = fixed.smem_len;
    }
    display->memory = (unsigned char *)mmap(NULL, map_size, PROT_READ |
                                                PROT_WRITE, MAP_SHARED,
                                            display->fd, 0);
    if (display->memory == MAP_FAILED) {
        int saved = errno;

        display->memory = NULL;
        close(display->fd);
        display->fd = -1;
        errno = saved;
        return -1;
    }
    display->memory_size = map_size;
    display->owns_memory = 0;
    display->draw_offset = 0;
    display->pan_offset = 0;

    gothgfx_unblank_framebuffer(display, path);
    gothgfx_pan_framebuffer(display, 0);

    if (variable.red.length != 0U || variable.green.length != 0U ||
        variable.blue.length != 0U) {
        display->red_offset = variable.red.offset;
        display->red_length = variable.red.length;
        display->green_offset = variable.green.offset;
        display->green_length = variable.green.length;
        display->blue_offset = variable.blue.offset;
        display->blue_length = variable.blue.length;
        display->alpha_offset = variable.transp.offset;
        display->alpha_length = variable.transp.length;
        display->grayscale = 0;
        if (display->bytes_per_pixel == 1) {
            display->grayscale = 1;
        }
    } else {
        gothgfx_default_format(display);
    }
    return 0;
#else
    (void)display;
    (void)path;
    (void)double_buffer;
    errno = ENOSYS;
    return -1;
#endif
}

static void gothgfx_display_close(GothGfxDisplay *display)
{
    int index;

    for (index = 0; index < display->input_count; index++) {
        gothgfx_input_close(&display->inputs[index]);
    }
    display->input_count = 0;

#if defined(__linux__)
    if (display->memory != NULL && !display->owns_memory) {
        (void)munmap(display->memory, display->memory_size);
    }
#endif
    if (display->owns_memory) {
        free(display->memory);
    }
    display->memory = NULL;
    display->memory_size = 0;
    if (display->fd >= 0) {
        close(display->fd);
        display->fd = -1;
    }
    display->open = 0;
}

static void gothgfx_update_logical_size(GothGfxDisplay *display)
{
    if (display->rotation == 90 || display->rotation == 270) {
        display->logical_width = display->height;
        display->logical_height = display->width;
    } else {
        display->logical_width = display->width;
        display->logical_height = display->height;
    }
}

/* -------------------------------------------------------------------------- */
/* Lua helpers                                                                */
/* -------------------------------------------------------------------------- */

static GothGfxDisplay *gothgfx_check_display(lua_State *L, int index)
{
    return (GothGfxDisplay *)luaL_checkudata(L, index, "gothgfx.display");
}

static int gothgfx_display_gc(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);

    if (display->open) {
        gothgfx_display_close(display);
    }
    return 0;
}

static int gothgfx_display_tostring(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);

    lua_pushfstring(L, "gothgfx.display(%dx%d, %d bpp)",
                    display->logical_width, display->logical_height,
                    display->bpp);
    return 1;
}

static int gothgfx_lua_open(lua_State *L)
{
    GothGfxDisplay *display;
    const char *framebuffer = "/dev/graphics/fb0";
    int use_memory = 0;
    int double_buffer = 0;
    int rotation = 0;
    int width = 0;
    int height = 0;
    int bpp = 0;
    lua_Integer value;
    int index;

    if (!lua_isnoneornil(L, 1) && !lua_istable(L, 1)) {
        return luaL_error(L, "gothgfx.open expects an options table");
    }

    if (lua_istable(L, 1)) {
        const char *path = gothgfx_get_string(L, 1, "fb");

        if (path != NULL) {
            framebuffer = path;
        }
        use_memory = gothgfx_get_boolean(L, 1, "memory", 0);
        double_buffer = gothgfx_get_boolean(L, 1, "double", 0);
        if (!gothgfx_get_integer(L, 1, "width", 0, &value)) {
            return luaL_error(L, "width must be a number");
        }
        width = (int)value;
        if (!gothgfx_get_integer(L, 1, "height", 0, &value)) {
            return luaL_error(L, "height must be a number");
        }
        height = (int)value;
        if (!gothgfx_get_integer(L, 1, "bpp", 0, &value)) {
            return luaL_error(L, "bpp must be a number");
        }
        bpp = (int)value;
        if (!gothgfx_get_integer(L, 1, "rotate", 0, &value)) {
            return luaL_error(L, "rotate must be a number");
        }
        rotation = (int)value;
    }

    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        return luaL_error(L, "rotate must be 0, 90, 180 or 270");
    }

    display = (GothGfxDisplay *)lua_newuserdatauv(L, sizeof(*display), 0);
    memset(display, 0, sizeof(*display));
    display->fd = -1;
    display->rotation = rotation;
    display->swap_xy = lua_istable(L, 1) &&
                       gothgfx_get_boolean(L, 1, "swap_xy", 0);
    display->invert_x = lua_istable(L, 1) &&
                        gothgfx_get_boolean(L, 1, "invert_x", 0);
    display->invert_y = lua_istable(L, 1) &&
                        gothgfx_get_boolean(L, 1, "invert_y", 0);
    if (lua_istable(L, 1)) {
        lua_Integer x_min;
        lua_Integer x_max;
        lua_Integer y_min;
        lua_Integer y_max;

        if (!gothgfx_get_integer(L, 1, "x_min", INT_MIN, &x_min) ||
            !gothgfx_get_integer(L, 1, "x_max", INT_MIN, &x_max) ||
            !gothgfx_get_integer(L, 1, "y_min", INT_MIN, &y_min) ||
            !gothgfx_get_integer(L, 1, "y_max", INT_MIN, &y_max)) {
            lua_pop(L, 1);
            return luaL_error(L, "touch ranges must be numbers");
        }
        if (x_min != INT_MIN && x_max != INT_MIN && x_max > x_min) {
            display->touch_x_min = (int)x_min;
            display->touch_x_max = (int)x_max;
            display->touch_x_range = 1;
        }
        if (y_min != INT_MIN && y_max != INT_MIN && y_max > y_min) {
            display->touch_y_min = (int)y_min;
            display->touch_y_max = (int)y_max;
            display->touch_y_range = 1;
        }
    }

    luaL_getmetatable(L, "gothgfx.display");
    lua_setmetatable(L, -2);

    if (use_memory || framebuffer == NULL || framebuffer[0] == '\0') {
        if (width <= 0 || height <= 0) {
            lua_pop(L, 1);
            return luaL_error(L, "memory surfaces need width and height");
        }
        if (gothgfx_open_memory(display, width, height, bpp) != 0) {
            lua_pop(L, 1);
            return luaL_error(L, "cannot create memory surface: %s",
                              gothgfx_errno_text(errno));
        }
    } else if (gothgfx_open_framebuffer(display, framebuffer,
                                        double_buffer) != 0) {
        int saved = errno;

        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L, "cannot open %s: %s", framebuffer,
                        gothgfx_errno_text(saved));
        return 2;
    }

    if (width > 0 && use_memory) {
        display->width = width;
    }
    if (height > 0 && use_memory) {
        display->height = height;
    }
    gothgfx_update_logical_size(display);

    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "input");
        if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
            lua_pop(L, 1);
        } else if (lua_isstring(L, -1)) {
            (void)gothgfx_add_input_path(display, lua_tostring(L, -1));
            lua_pop(L, 1);
        } else if (lua_istable(L, -1)) {
            lua_Integer count = (lua_Integer)luaL_len(L, -1);

            for (index = 1; index <= (int)count; index++) {
                lua_rawgeti(L, -1, index);
                if (lua_isstring(L, -1)) {
                    (void)gothgfx_add_input_path(display, lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
            gothgfx_add_auto_inputs(display);
        }
    } else {
        gothgfx_add_auto_inputs(display);
    }
    display->open = 1;
    return 1;
}

static int gothgfx_lua_create(lua_State *L)
{
    int width = (int)luaL_checkinteger(L, 1);
    int height = (int)luaL_checkinteger(L, 2);
    int bpp = (int)luaL_optinteger(L, 3, 32);
    GothGfxDisplay *display;

    display = (GothGfxDisplay *)lua_newuserdatauv(L, sizeof(*display), 0);
    memset(display, 0, sizeof(*display));
    display->fd = -1;
    display->rotation = 0;
    luaL_getmetatable(L, "gothgfx.display");
    lua_setmetatable(L, -2);
    if (gothgfx_open_memory(display, width, height, bpp) != 0) {
        lua_pop(L, 1);
        return luaL_error(L, "cannot create memory surface: %s",
                          gothgfx_errno_text(errno));
    }
    gothgfx_update_logical_size(display);
    display->open = 1;
    return 1;
}

static int gothgfx_lua_info(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);

    lua_createtable(L, 0, 10);
    lua_pushinteger(L, display->width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, display->height);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, display->logical_width);
    lua_setfield(L, -2, "logical_width");
    lua_pushinteger(L, display->logical_height);
    lua_setfield(L, -2, "logical_height");
    lua_pushinteger(L, display->bpp);
    lua_setfield(L, -2, "bpp");
    lua_pushinteger(L, display->stride);
    lua_setfield(L, -2, "stride");
    lua_pushinteger(L, display->rotation);
    lua_setfield(L, -2, "rotation");
    lua_pushboolean(L, display->double_buffer);
    lua_setfield(L, -2, "double_buffer");
    lua_pushinteger(L, display->input_count);
    lua_setfield(L, -2, "inputs");
    return 1;
}

static int gothgfx_lua_size(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);

    lua_pushinteger(L, display->logical_width);
    lua_pushinteger(L, display->logical_height);
    return 2;
}

static int gothgfx_lua_clear(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    lua_Integer color = luaL_optinteger(L, 2, 0);

    gothgfx_fill_rect(display, 0, 0, display->logical_width,
                      display->logical_height, gothgfx_pack_color(display, color));
    return 1;
}

static int gothgfx_lua_pixel(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    lua_Integer color = luaL_checkinteger(L, 4);

    gothgfx_put_pixel(display, x, y, gothgfx_pack_color(display, color));
    return 1;
}

static int gothgfx_lua_getpixel(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int native_x;
    int native_y;
    size_t offset;
    uint32_t packed;

    if (!gothgfx_map_point(display, x, y, &native_x, &native_y)) {
        return 0;
    }
    offset = display->draw_offset +
             (size_t)native_y * (size_t)display->stride +
             (size_t)native_x * (size_t)display->bytes_per_pixel;
    packed = gothgfx_load_pixel(display, offset);
    lua_pushinteger(L, (lua_Integer)gothgfx_unpack_color(display, packed));
    return 1;
}

static int gothgfx_lua_fill(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int width = (int)luaL_checkinteger(L, 4);
    int height = (int)luaL_checkinteger(L, 5);
    lua_Integer color = luaL_checkinteger(L, 6);

    gothgfx_fill_rect(display, x, y, width, height,
                      gothgfx_pack_color(display, color));
    return 1;
}

static int gothgfx_lua_rect(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int width = (int)luaL_checkinteger(L, 4);
    int height = (int)luaL_checkinteger(L, 5);
    lua_Integer color = luaL_checkinteger(L, 6);
    int thickness = (int)luaL_optinteger(L, 7, 1);

    gothgfx_draw_rect(display, x, y, width, height, thickness,
                      gothgfx_pack_color(display, color));
    return 1;
}

static int gothgfx_lua_line(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int x0 = (int)luaL_checkinteger(L, 2);
    int y0 = (int)luaL_checkinteger(L, 3);
    int x1 = (int)luaL_checkinteger(L, 4);
    int y1 = (int)luaL_checkinteger(L, 5);
    lua_Integer color = luaL_checkinteger(L, 6);

    gothgfx_draw_line(display, x0, y0, x1, y1,
                      gothgfx_pack_color(display, color));
    return 1;
}

static int gothgfx_lua_text(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    size_t length = 0;
    const char *text = luaL_checklstring(L, 4, &length);
    lua_Integer color = luaL_optinteger(L, 5, 0xffffff);
    int scale = (int)luaL_optinteger(L, 6, 1);
    int width;

    (void)length;
    width = gothgfx_draw_text(display, x, y, text, scale,
                              gothgfx_pack_color(display, color));
    lua_pushinteger(L, width);
    lua_pushinteger(L, GOTHGFX_FONT_HEIGHT * (scale < 1 ? 1 : scale));
    return 2;
}

static int gothgfx_lua_measure(lua_State *L)
{
    size_t length = 0;
    int scale;

    (void)gothgfx_check_display(L, 1);
    (void)luaL_checklstring(L, 2, &length);
    scale = (int)luaL_optinteger(L, 3, 1);

    if (scale < 1) {
        scale = 1;
    }
    lua_pushinteger(L, (lua_Integer)length * GOTHGFX_FONT_WIDTH * scale);
    lua_pushinteger(L, GOTHGFX_FONT_HEIGHT * scale);
    return 2;
}

static int gothgfx_lua_present(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);

#if defined(__linux__)
    if (display->fd >= 0) {
        /* Even for a single-buffered surface, panning to the drawing offset
         * re-asserts our buffer as the scanout source.  Harmless when the
         * driver already shows it, essential when it does not. */
        gothgfx_pan_framebuffer(display, display->draw_offset);
        if (display->double_buffer) {
            display->pan_offset = display->draw_offset;
            display->draw_offset =
                display->draw_offset == 0U
                    ? (uint32_t)((size_t)display->stride * display->height)
                    : 0U;
        }
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
}

static int gothgfx_lua_poll_events(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    lua_Integer timeout = luaL_optinteger(L, 2, 0);
    GothGfxEvent event;
    int count = 0;

    if (timeout < 0) {
        timeout = 0;
    }
    if (display->queue_count == 0) {
        (void)gothgfx_poll_inputs(display, (int)timeout);
    }
    lua_newtable(L);
    while (gothgfx_dequeue_event(display, &event)) {
        lua_createtable(L, 0, 10);
        lua_pushstring(L, event.type == 1 ? "touch" : "key");
        lua_setfield(L, -2, "type");
        if (event.action == 0) {
            lua_pushstring(L, "down");
        } else if (event.action == 1) {
            lua_pushstring(L, "up");
        } else if (event.action == 2) {
            lua_pushstring(L, "move");
        } else {
            lua_pushstring(L, "cancel");
        }
        lua_setfield(L, -2, "action");
        lua_pushinteger(L, event.id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, event.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, event.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, event.raw_x);
        lua_setfield(L, -2, "raw_x");
        lua_pushinteger(L, event.raw_y);
        lua_setfield(L, -2, "raw_y");
        lua_pushinteger(L, event.code);
        lua_setfield(L, -2, "code");
        lua_pushinteger(L, (lua_Integer)event.time_ms);
        lua_setfield(L, -2, "time");
        lua_rawseti(L, -2, ++count);
    }
    return 1;
}

static int gothgfx_lua_wait_event(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    lua_Integer timeout = luaL_optinteger(L, 2,
                                          GOTHGFX_INPUT_WAIT_MS);
    GothGfxEvent event;
    uint64_t deadline;
    int remaining = (int)timeout;

    if (timeout < 0) {
        timeout = 0;
    }
    deadline = gothgfx_now_ms() + (uint64_t)timeout;
    while (display->queue_count == 0) {
        uint64_t now = gothgfx_now_ms();
        int wait_ms;

        if (now >= deadline) {
            return 0;
        }
        wait_ms = (int)(deadline - now);
        if (!gothgfx_poll_inputs(display, wait_ms)) {
            return 0;
        }
    }
    remaining = 0;
    (void)remaining;
    if (!gothgfx_dequeue_event(display, &event)) {
        return 0;
    }
    lua_createtable(L, 0, 10);
    lua_pushstring(L, event.type == 1 ? "touch" : "key");
    lua_setfield(L, -2, "type");
    if (event.action == 0) {
        lua_pushstring(L, "down");
    } else if (event.action == 1) {
        lua_pushstring(L, "up");
    } else if (event.action == 2) {
        lua_pushstring(L, "move");
    } else {
        lua_pushstring(L, "cancel");
    }
    lua_setfield(L, -2, "action");
    lua_pushinteger(L, event.id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, event.x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, event.y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, event.raw_x);
    lua_setfield(L, -2, "raw_x");
    lua_pushinteger(L, event.raw_y);
    lua_setfield(L, -2, "raw_y");
    lua_pushinteger(L, event.code);
    lua_setfield(L, -2, "code");
    lua_pushinteger(L, (lua_Integer)event.time_ms);
    lua_setfield(L, -2, "time");
    return 1;
}

static int gothgfx_lua_poll(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    lua_Integer timeout = luaL_optinteger(L, 2, 0);

    if (timeout < 0) {
        timeout = 0;
    }
    if (display->queue_count == 0) {
        (void)gothgfx_poll_inputs(display, (int)timeout);
    }
    lua_pushboolean(L, display->queue_count != 0);
    return 1;
}

static int gothgfx_lua_close(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);

    if (display->open) {
        gothgfx_display_close(display);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int gothgfx_lua_inputs(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int index;

    lua_createtable(L, display->input_count, 0);
    for (index = 0; index < display->input_count; index++) {
        lua_pushstring(L, display->inputs[index].path);
        lua_rawseti(L, -2, index + 1);
    }
    return 1;
}

static int gothgfx_lua_key_name(lua_State *L)
{
    int code = (int)luaL_checkinteger(L, 1);

    switch (code) {
    case KEY_BACK:
        lua_pushstring(L, "back");
        break;
    case KEY_HOME:
        lua_pushstring(L, "home");
        break;
    case KEY_MENU:
        lua_pushstring(L, "menu");
        break;
    case KEY_ENTER:
        lua_pushstring(L, "enter");
        break;
    case KEY_ESC:
        lua_pushstring(L, "escape");
        break;
    case KEY_VOLUMEUP:
        lua_pushstring(L, "volumeup");
        break;
    case KEY_VOLUMEDOWN:
        lua_pushstring(L, "volumedown");
        break;
    case KEY_POWER:
        lua_pushstring(L, "power");
        break;
    case KEY_UP:
        lua_pushstring(L, "up");
        break;
    case KEY_DOWN:
        lua_pushstring(L, "down");
        break;
    case KEY_LEFT:
        lua_pushstring(L, "left");
        break;
    case KEY_RIGHT:
        lua_pushstring(L, "right");
        break;
    default:
        lua_pushnil(L);
        break;
    }
    return 1;
}

static int gothgfx_lua_scan_inputs(lua_State *L)
{
    int count = 0;

    lua_newtable(L);
#if defined(__linux__)
    {
        DIR *directory = opendir("/dev/input");
        struct dirent *entry;

        if (directory != NULL) {
            while ((entry = readdir(directory)) != NULL) {
                char path[300];

                if (strncmp(entry->d_name, "event", 5) != 0) {
                    continue;
                }
                if (snprintf(path, sizeof(path), "/dev/input/%s",
                             entry->d_name) >= (int)sizeof(path)) {
                    continue;
                }
                if (gothgfx_looks_like_touchscreen(path)) {
                    lua_pushstring(L, path);
                    lua_rawseti(L, -2, ++count);
                }
            }
            closedir(directory);
        }
    }
#endif
    return 1;
}

static int gothgfx_lua_now(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)gothgfx_now_ms());
    return 1;
}

static int gothgfx_lua_sleep(lua_State *L)
{
    lua_Integer milliseconds = luaL_checkinteger(L, 1);

    if (milliseconds > 0) {
        struct timespec delay;

        delay.tv_sec = (time_t)(milliseconds / 1000);
        delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
    }
    return 0;
}

static int gothgfx_lua_set_rotation(lua_State *L)
{
    GothGfxDisplay *display = gothgfx_check_display(L, 1);
    int rotation = (int)luaL_checkinteger(L, 2);

    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        return luaL_error(L, "rotate must be 0, 90, 180 or 270");
    }
    display->rotation = rotation;
    gothgfx_update_logical_size(display);
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg gothgfx_display_methods[] = {
    {"info", gothgfx_lua_info},
    {"size", gothgfx_lua_size},
    {"clear", gothgfx_lua_clear},
    {"pixel", gothgfx_lua_pixel},
    {"getPixel", gothgfx_lua_getpixel},
    {"fill", gothgfx_lua_fill},
    {"fillRect", gothgfx_lua_fill},
    {"rect", gothgfx_lua_rect},
    {"line", gothgfx_lua_line},
    {"text", gothgfx_lua_text},
    {"measure", gothgfx_lua_measure},
    {"present", gothgfx_lua_present},
    {"flush", gothgfx_lua_present},
    {"pollEvents", gothgfx_lua_poll_events},
    {"waitEvent", gothgfx_lua_wait_event},
    {"poll", gothgfx_lua_poll},
    {"close", gothgfx_lua_close},
    {"inputs", gothgfx_lua_inputs},
    {"setRotation", gothgfx_lua_set_rotation},
    {NULL, NULL}
};

static const luaL_Reg gothgfx_module_functions[] = {
    {"open", gothgfx_lua_open},
    {"create", gothgfx_lua_create},
    {"scanInputs", gothgfx_lua_scan_inputs},
    {"now", gothgfx_lua_now},
    {"sleep", gothgfx_lua_sleep},
    {"keyName", gothgfx_lua_key_name},
    {NULL, NULL}
};

int luaopen_gothgfx(lua_State *L)
{
    luaL_newmetatable(L, "gothgfx.display");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, gothgfx_display_methods, 0);
    lua_pushcfunction(L, gothgfx_display_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, gothgfx_display_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushstring(L, "gothgfx.display");
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);

    luaL_newlib(L, gothgfx_module_functions);
    lua_pushstring(L, "GothRTOS");
    lua_setfield(L, -2, "name");
    lua_pushstring(L, "1.0");
    lua_setfield(L, -2, "version");
    lua_pushinteger(L, GOTHGFX_FONT_WIDTH);
    lua_setfield(L, -2, "fontWidth");
    lua_pushinteger(L, GOTHGFX_FONT_HEIGHT);
    lua_setfield(L, -2, "fontHeight");
    lua_pushinteger(L, KEY_BACK);
    lua_setfield(L, -2, "KEY_BACK");
    lua_pushinteger(L, KEY_HOME);
    lua_setfield(L, -2, "KEY_HOME");
    lua_pushinteger(L, KEY_MENU);
    lua_setfield(L, -2, "KEY_MENU");
    lua_pushinteger(L, KEY_ENTER);
    lua_setfield(L, -2, "KEY_ENTER");
    lua_pushinteger(L, KEY_VOLUMEUP);
    lua_setfield(L, -2, "KEY_VOLUMEUP");
    lua_pushinteger(L, KEY_VOLUMEDOWN);
    lua_setfield(L, -2, "KEY_VOLUMEDOWN");
    lua_pushinteger(L, KEY_POWER);
    lua_setfield(L, -2, "KEY_POWER");
    return 1;
}
