#include <stdint.h>
#include <stdlib.h>

#include "gothgfx.h"
#include "lauxlib.h"
#include "lvgl.h"

typedef struct GothGfxDisplay GothGfxDisplay;
typedef struct GothGfxEvent {
    int type, action, id, x, y, raw_x, raw_y, code;
    uint64_t time_ms;
} GothGfxEvent;
typedef struct {
    GothGfxDisplay *surface;
    lv_indev_t *pointer;
    int32_t x;
    int32_t y;
    int pressed;
} GothLvContext;

extern void gothgfx_lvgl_copy_area(GothGfxDisplay *, int, int, int, int,
                                   const uint8_t *);
extern int gothgfx_lvgl_size(GothGfxDisplay *, int *, int *);
extern int gothgfx_lvgl_poll_event(void *, GothGfxEvent *);

static void flush_cb(lv_display_t *display, const lv_area_t *area,
                     uint8_t *pixels)
{
    GothLvContext *context = lv_display_get_user_data(display);
    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    gothgfx_lvgl_copy_area(context->surface, area->x1, area->y1, width,
                           height, pixels);
    lv_display_flush_ready(display);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    GothLvContext *context = lv_indev_get_user_data(indev);
    GothGfxEvent event;
    while (gothgfx_lvgl_poll_event(context->surface, &event)) {
        if (event.type == 1) {
            context->x = event.x;
            context->y = event.y;
            context->pressed = event.action == 0 || event.action == 2;
            break;
        }
    }
    data->point.x = context->x;
    data->point.y = context->y;
    data->state = context->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static int lv_loop(lua_State *L)
{
    GothLvContext *context = (GothLvContext *)luaL_checkudata(L, 1, "gothlv.context");
    uint32_t delay = lv_timer_handler();
    (void)context;
    lua_pushinteger(L, (lua_Integer)delay);
    return 1;
}

static int lv_close(lua_State *L)
{
    GothLvContext *context = (GothLvContext *)luaL_checkudata(L, 1, "gothlv.context");
    if (context->pointer != NULL) lv_indev_delete(context->pointer);
    context->pointer = NULL;
    lua_pushboolean(L, 1);
    return 1;
}

static int lv_open(lua_State *L)
{
    GothGfxDisplay *surface = (GothGfxDisplay *)luaL_checkudata(L, 1, "gothgfx.display");
    GothLvContext *context = (GothLvContext *)lua_newuserdatauv(L, sizeof(*context), 0);
    lv_display_t *display;
    lv_color_t *buffer;
    int width, height;
    size_t pixels;
    if (gothgfx_lvgl_size(surface, &width, &height) != 0)
        return luaL_error(L, "graphics display is closed");
    context->surface = surface;
    context->x = context->y = 0;
    context->pressed = 0;
    context->pointer = NULL;
    luaL_getmetatable(L, "gothlv.context");
    lua_setmetatable(L, -2);
    pixels = (size_t)width * (size_t)height;
    buffer = (lv_color_t *)malloc(pixels * sizeof(lv_color_t));
    if (buffer == NULL) return luaL_error(L, "cannot allocate LVGL draw buffer");
    lv_init();
    display = lv_display_create(width, height);
    if (display == NULL) { free(buffer); return luaL_error(L, "cannot create LVGL display"); }
    lv_display_set_user_data(display, context);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_buffers(display, buffer, NULL,
                           (uint32_t)(pixels * sizeof(lv_color_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    context->pointer = lv_indev_create();
    lv_indev_set_type(context->pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(context->pointer, read_cb);
    lv_indev_set_user_data(context->pointer, context);
    lv_indev_set_display(context->pointer, display);
    return 1;
}

int luaopen_gothlv(lua_State *L)
{
    static const luaL_Reg methods[] = {{"loop", lv_loop}, {"close", lv_close}, {NULL, NULL}};
    luaL_newmetatable(L, "gothlv.context");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, methods, 0);
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, lv_open);
    lua_setfield(L, -2, "open");
    return 1;
}
