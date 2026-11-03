/*
 * gothdroid -- the native GothRTOS runtime for Android devices.
 *
 * The Android app installs this executable and its Lua userland, then starts
 * it through `su` after stopping the Android framework.  The runtime owns the
 * framebuffer and the touchscreen, embeds Lua, publishes the gothgfx module and
 * executes SystemUI.lua.
 *
 * This is the real GothRTOS kernel: rtos/kernel.c initializes the static task
 * table, IPC endpoints and syscalls, SystemUI.lua runs as a GothRTOS task with
 * the goth userland API from os/init/gothlib.c, and goth_um_run() drives the
 * cooperative scheduler.  Framebuffer and evdev I/O remain userspace policy
 * (libgothgfx), exactly as on the other ports; the kernel only schedules and
 * passes messages.
 *
 * The process itself does not stop/start Android: that is the job of the root
 * supervisor script launched by the app, so /system/sbin/start still runs even
 * if this runtime crashes or is killed.
 */

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

extern "C" {
#include "gothrtos/gothlib.h"
#include "gothrtos/kernel.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "gothgfx.h"

namespace {

volatile std::sig_atomic_t g_stopping = 0;
std::FILE *g_log_file = nullptr;
std::string g_stop_file;

void signal_handler(int signal_number)
{
    (void)signal_number;
    g_stopping = 1;
}

void gothdroid_log(const char *format, ...)
{
    char buffer[2048];
    va_list arguments;

    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "gothdroid", "%s", buffer);
#endif
    std::fprintf(stderr, "gothdroid: %s\n", buffer);
    if (g_log_file != nullptr) {
        std::fprintf(g_log_file, "gothdroid: %s\n", buffer);
        std::fflush(g_log_file);
    }
}

bool file_exists(const std::string &path)
{
    struct stat information;

    return !path.empty() && stat(path.c_str(), &information) == 0;
}

std::string directory_of(const std::string &path)
{
    std::string::size_type slash = path.find_last_of('/');

    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string executable_path()
{
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

    if (length <= 0) {
        return {};
    }
    buffer[length] = '\0';
    return std::string(buffer);
}

bool make_directory(const std::string &path)
{
    if (path.empty() || path == "/" || file_exists(path)) {
        return true;
    }
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

bool make_directories(const std::string &path)
{
    std::string current;
    std::string::size_type position = 0;

    if (path.empty()) {
        return false;
    }
    if (path[0] == '/') {
        current = "/";
        position = 1;
    }
    while (position <= path.size()) {
        std::string::size_type slash = path.find('/', position);
        std::string component =
            path.substr(position, slash == std::string::npos
                                       ? std::string::npos
                                       : slash - position);

        if (!component.empty()) {
            if (current.size() > 1 && current.back() != '/') {
                current += '/';
            }
            current += component;
            if (!make_directory(current)) {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        position = slash + 1;
    }
    return true;
}

std::string read_stop_file()
{
    return g_stop_file;
}

int lua_gothdroid_stopping(lua_State *state)
{
    lua_pushboolean(state, g_stopping != 0 || file_exists(read_stop_file()));
    return 1;
}

int lua_gothdroid_request_stop(lua_State *state)
{
    const char *path = luaL_optstring(state, 1, g_stop_file.c_str());

    g_stopping = 1;
    if (path != nullptr && path[0] != '\0') {
        std::FILE *file = std::fopen(path, "wb");
        if (file != nullptr) {
            std::fputs("stop\n", file);
            std::fclose(file);
        }
    }
    lua_pushboolean(state, 1);
    return 1;
}

int lua_gothdroid_log(lua_State *state)
{
    const char *message = luaL_checkstring(state, 1);

    gothdroid_log("%s", message);
    return 0;
}

int lua_gothdroid_sleep(lua_State *state)
{
    lua_Integer milliseconds = luaL_checkinteger(state, 1);

    if (milliseconds > 0) {
        struct timespec delay;

        delay.tv_sec = static_cast<time_t>(milliseconds / 1000);
        delay.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
    }
    return 0;
}

void register_gothdroid(lua_State *state, const std::string &script,
                        const std::string &root,
                        const std::vector<std::string> &arguments)
{
    lua_createtable(state, 0, 12);
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "stopping");

    lua_pushcfunction(state, lua_gothdroid_stopping);
    lua_setfield(state, -2, "isStopping");
    lua_pushcfunction(state, lua_gothdroid_request_stop);
    lua_setfield(state, -2, "requestStop");
    lua_pushcfunction(state, lua_gothdroid_log);
    lua_setfield(state, -2, "log");
    lua_pushcfunction(state, lua_gothdroid_sleep);
    lua_setfield(state, -2, "sleep");

    lua_pushstring(state, script.c_str());
    lua_setfield(state, -2, "script");
    lua_pushstring(state, root.c_str());
    lua_setfield(state, -2, "root");
    lua_pushstring(state, g_stop_file.c_str());
    lua_setfield(state, -2, "stopFile");
    lua_pushstring(state, "android");
    lua_setfield(state, -2, "platform");
    lua_pushstring(state, "1.0");
    lua_setfield(state, -2, "version");

    lua_createtable(state, static_cast<int>(arguments.size()), 0);
    for (std::size_t index = 0; index < arguments.size(); index++) {
        lua_pushstring(state, arguments[index].c_str());
        lua_rawseti(state, -2, static_cast<lua_Integer>(index) + 1);
    }
    lua_setfield(state, -2, "argv");

    lua_setglobal(state, "gothdroid");
}

void prepend_package_path(lua_State *state, const std::string &path)
{
    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    lua_getfield(state, -1, "path");
    if (lua_isstring(state, -1)) {
        std::string existing = lua_tostring(state, -1);
        std::string combined = path + ";" + existing;
        lua_pop(state, 1);
        lua_pushstring(state, combined.c_str());
        lua_setfield(state, -2, "path");
    } else {
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
}

void prepend_package_cpath(lua_State *state, const std::string &path)
{
    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    lua_getfield(state, -1, "cpath");
    if (lua_isstring(state, -1)) {
        std::string existing = lua_tostring(state, -1);
        std::string combined = path + ";" + existing;
        lua_pop(state, 1);
        lua_pushstring(state, combined.c_str());
        lua_setfield(state, -2, "cpath");
    } else {
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
}

int report_lua_error(lua_State *state)
{
    const char *message = lua_tostring(state, -1);

    gothdroid_log("Lua error: %s", message != nullptr ? message : "(no message)");
    lua_pop(state, 1);
    return 1;
}

struct Options {
    std::string script;
    std::string root;
    std::string framebuffer = "/dev/graphics/fb0";
    std::string input;
    std::string device;
    std::string log_file;
    std::string stop_file;
    std::vector<std::string> arguments;
    int rotate = -1;
    int width = 0;
    int height = 0;
    int bpp = 0;
    bool list_inputs = false;
    bool fb_probe = false;
};

Options parse_arguments(int argc, char **argv)
{
    Options options;

    for (int index = 1; index < argc; index++) {
        std::string argument = argv[index];
        auto next = [&](const char *name) -> std::string {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "gothdroid: %s needs a value\n", name);
                std::exit(2);
            }
            return std::string(argv[++index]);
        };

        if (argument == "--script") {
            options.script = next("--script");
        } else if (argument == "--root") {
            options.root = next("--root");
        } else if (argument == "--fb" || argument == "--framebuffer") {
            options.framebuffer = next("--fb");
        } else if (argument == "--input") {
            options.input = next("--input");
        } else if (argument == "--device") {
            options.device = next("--device");
        } else if (argument == "--log") {
            options.log_file = next("--log");
        } else if (argument == "--stop-file") {
            options.stop_file = next("--stop-file");
        } else if (argument == "--rotate") {
            options.rotate = std::atoi(next("--rotate").c_str());
        } else if (argument == "--width") {
            options.width = std::atoi(next("--width").c_str());
        } else if (argument == "--height") {
            options.height = std::atoi(next("--height").c_str());
        } else if (argument == "--bpp") {
            options.bpp = std::atoi(next("--bpp").c_str());
        } else if (argument == "--list-inputs") {
            options.list_inputs = true;
        } else if (argument == "--fb-probe") {
            options.fb_probe = true;
        } else if (argument == "--help" || argument == "-h") {
            std::printf(
                "usage: gothdroid [options] [-- script arguments]\n"
                "  --script PATH      Lua UI script (default SystemUI.lua)\n"
                "  --root PATH        GothRTOS userland root\n"
                "  --fb PATH          framebuffer (default /dev/graphics/fb0)\n"
                "  --input PATH       touchscreen event device (repeatable)\n"
                "  --device NAME      load devices/NAME.lua in SystemUI\n"
                "  --rotate DEG       rotate the UI (0, 90, 180, 270)\n"
                "  --width N          override framebuffer width\n"
                "  --height N         override framebuffer height\n"
                "  --bpp N            override framebuffer bits per pixel\n"
                "  --log PATH         append runtime diagnostics to PATH\n"
                "  --stop-file PATH   exit when PATH appears\n"
                "  --list-inputs      print detected touch devices and exit\n"
                "  --fb-probe         draw framebuffer test bars, print geometry\n"
                "                     and exit (use after `/system/sbin/stop`)\n");
            std::exit(0);
        } else if (argument == "--") {
            for (int rest = index + 1; rest < argc; rest++) {
                options.arguments.emplace_back(argv[rest]);
            }
            break;
        } else if (argument.size() > 1 && argument[0] == '-') {
            std::fprintf(stderr, "gothdroid: unknown option %s\n",
                         argument.c_str());
            std::exit(2);
        } else {
            options.arguments.push_back(argument);
        }
    }
    return options;
}

std::string find_script(const std::string &requested,
                        const std::string &root,
                        const std::string &executable)
{
    std::vector<std::string> candidates;

    if (!requested.empty()) {
        candidates.push_back(requested);
    }
    const char *environment_script = std::getenv("GOTHDROID_SCRIPT");
    if (environment_script != nullptr && environment_script[0] != '\0') {
        candidates.emplace_back(environment_script);
    }
    if (!root.empty()) {
        candidates.push_back(root + "/SystemUI/SystemUI.lua");
        candidates.push_back(root + "/SystemUI.lua");
        candidates.push_back(root + "/init.d/SystemUI.lua");
    }
    if (!executable.empty()) {
        std::string directory = directory_of(executable);
        candidates.push_back(directory + "/SystemUI/SystemUI.lua");
        candidates.push_back(directory + "/SystemUI.lua");
    }
    candidates.emplace_back("/data/local/tmp/gothrtos/SystemUI/SystemUI.lua");
    candidates.emplace_back("os/Android/SystemUI/SystemUI.lua");
    candidates.emplace_back("SystemUI/SystemUI.lua");

    for (const std::string &candidate : candidates) {
        if (file_exists(candidate)) {
            return candidate;
        }
    }
    return requested.empty() ? candidates.front() : requested;
}

std::string find_root(const std::string &requested,
                      const std::string &script,
                      const std::string &executable)
{
    if (!requested.empty()) {
        return requested;
    }
    const char *environment_root = std::getenv("GOTHDROID_ROOT");
    if (environment_root != nullptr && environment_root[0] != '\0') {
        return environment_root;
    }
    if (!script.empty()) {
        std::string directory = directory_of(script);
        if (directory.size() > 9 &&
            directory.compare(directory.size() - 9, 9, "/SystemUI") == 0) {
            return directory.substr(0, directory.size() - 9);
        }
        return directory;
    }
    if (!executable.empty()) {
        return directory_of(executable);
    }
    return "/data/local/tmp/gothrtos";
}

void open_log_file(const std::string &path)
{
    if (path.empty()) {
        return;
    }
    make_directories(directory_of(path));
    g_log_file = std::fopen(path.c_str(), "ab");
    if (g_log_file == nullptr) {
        std::fprintf(stderr, "gothdroid: cannot open log %s: %s\n",
                     path.c_str(), std::strerror(errno));
    }
}

/* The UI runs as a real GothRTOS task.  pthreads get the stack size from
 * pthread_attr_setstacksize(), and 512 KiB is comfortable for Lua and
 * libgothgfx. */
constexpr std::size_t kUiStackSize = 512U * 1024U;

struct UiRequest {
    Options options;
    std::string script;
    std::string root;
};

UiRequest g_ui_request;
int g_ui_status = 1;
alignas(16) unsigned char g_ui_stack[kUiStackSize];
goth_task_t g_ui_task = {};

int run_ui(const UiRequest &request);

void ui_task_entry(void *argument)
{
    (void)argument;
    g_ui_status = run_ui(g_ui_request);
    /* Never returns: goth_task_exit() terminates this task and lets the
     * scheduler run the remaining tasks (there are none here). */
    (void)goth_task_exit();
}

/* Publishes the command-line options as the `gothdroid_options` global. */
void publish_options(lua_State *state, const Options &options)
{
    lua_createtable(state, 0, 8);
    lua_pushstring(state, options.framebuffer.c_str());
    lua_setfield(state, -2, "fb");
    if (!options.input.empty()) {
        lua_pushstring(state, options.input.c_str());
        lua_setfield(state, -2, "input");
    }
    if (!options.device.empty()) {
        lua_pushstring(state, options.device.c_str());
        lua_setfield(state, -2, "device");
    }
    if (options.rotate >= 0) {
        lua_pushinteger(state, options.rotate);
        lua_setfield(state, -2, "rotate");
    }
    if (options.width > 0) {
        lua_pushinteger(state, options.width);
        lua_setfield(state, -2, "width");
    }
    if (options.height > 0) {
        lua_pushinteger(state, options.height);
        lua_setfield(state, -2, "height");
    }
    if (options.bpp > 0) {
        lua_pushinteger(state, options.bpp);
        lua_setfield(state, -2, "bpp");
    }
    lua_setglobal(state, "gothdroid_options");
}

/* Body of the GothRTOS UI task: build the Lua state, publish the goth
 * userland API, run SystemUI.lua, and return 0 on success. */
int run_ui(const UiRequest &request)
{
    const Options &options = request.options;
    const std::string &script = request.script;
    const std::string &root = request.root;

    lua_State *state = luaL_newstate();
    if (state == nullptr) {
        gothdroid_log("cannot create Lua state");
        return 1;
    }
    luaL_openlibs(state);
    if (goth_lua_open_userapi(state) != 0) {
        gothdroid_log("cannot publish the goth userland API");
        lua_close(state);
        return 1;
    }
    luaL_requiref(state, "gothgfx", luaopen_gothgfx, 1);
    lua_pop(state, 1);

    std::string modules = root + "/?.lua;" + root + "/?/init.lua;" +
                          root + "/SystemUI/?.lua;" + root + "/lib/?.lua;" +
                          directory_of(script) + "/?.lua";
    prepend_package_path(state, modules);
    prepend_package_cpath(state, root + "/lib/?.so;" + root + "/lib/lib?.so;" +
                                 root + "/?.so;" + root + "/lib?.so");

    publish_options(state, options);

    register_gothdroid(state, script, root, options.arguments);

    int status = luaL_loadfilex(state, script.c_str(), nullptr);
    if (status == LUA_OK) {
        status = lua_pcall(state, 0, 0, 0);
    }
    if (status != LUA_OK) {
        report_lua_error(state);
        lua_close(state);
        return 1;
    }

    lua_close(state);
    return 0;
}

/*
 * Framebuffer diagnostic: open the panel exactly like SystemUI does, print the
 * geometry the kernel reports, paint colour bars and keep them up for a few
 * seconds.  Run it after "/system/sbin/stop" on a device where the UI starts
 * but nothing reaches the screen.
 */
int run_fb_probe(const Options &options)
{
    static const char probe[] =
        "local gfx = require('gothgfx')\n"
        "for index = 0, 3 do\n"
        "  local base = '/sys/class/graphics/fb' .. index\n"
        "  local function read(name)\n"
        "    local file = io.open(base .. '/' .. name, 'r')\n"
        "    if not file then return nil end\n"
        "    local value = file:read('*l')\n"
        "    file:close()\n"
        "    return value\n"
        "  end\n"
        "  local name = read('name')\n"
        "  if name then\n"
        "    io.write(string.format('gothdroid: fb%d: %s virtual=%s blank=%s\\n',\n"
        "      index, name, tostring(read('virtual_size')),\n"
        "      tostring(read('blank'))))\n"
        "  end\n"
        "end\n"
        "local ok, display, message = pcall(gfx.open, gothdroid_options or {})\n"
        "if not ok or display == nil then\n"
        "  io.write('gothdroid: framebuffer open failed: ' ..\n"
        "           tostring(ok and message or display) .. '\\n')\n"
        "  return 1\n"
        "end\n"
        "local keys = { 'width', 'height', 'logical_width', 'logical_height',\n"
        "               'bpp', 'stride', 'rotation', 'double_buffer', 'inputs' }\n"
        "local info = display:info()\n"
        "for _, key in ipairs(keys) do\n"
        "  io.write(string.format('gothdroid: fb %s = %s\\n', key,\n"
        "                         tostring(info[key])))\n"
        "end\n"
        "local width, height = display:size()\n"
        "local bands = { 0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x000000 }\n"
        "local band = math.max(1, math.floor(height / #bands))\n"
        "for index, colour in ipairs(bands) do\n"
        "  display:fill(0, (index - 1) * band, width, band, colour)\n"
        "end\n"
        "display:text(20, 20, 'GothRTOS framebuffer probe', 0xf4f7ff, 2)\n"
        "display:present()\n"
        "io.write('gothdroid: colour bars on screen for 5 seconds\\n')\n"
        "io.flush()\n"
        "gfx.sleep(5000)\n"
        "display:close()\n"
        "return 0\n";

    lua_State *state = luaL_newstate();
    if (state == nullptr) {
        gothdroid_log("cannot create Lua state");
        return 1;
    }
    luaL_openlibs(state);
    luaL_requiref(state, "gothgfx", luaopen_gothgfx, 1);
    lua_pop(state, 1);
    publish_options(state, options);

    int status = luaL_loadbufferx(state, probe, std::strlen(probe),
                                  "=fb-probe", nullptr);
    int result = 0;
    if (status == LUA_OK) {
        status = lua_pcall(state, 0, 1, 0);
    }
    if (status != LUA_OK) {
        report_lua_error(state);
        lua_close(state);
        return 1;
    }
    if (lua_isnumber(state, -1)) {
        result = static_cast<int>(lua_tointeger(state, -1));
    }
    lua_close(state);
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    Options options = parse_arguments(argc, argv);
    std::string executable = executable_path();
    std::string script = find_script(options.script, options.root,
                                      executable);
    std::string root = find_root(options.root, script, executable);
    if (options.stop_file.empty()) {
        const char *environment_stop = std::getenv("GOTHDROID_STOP_FILE");
        options.stop_file = environment_stop != nullptr
                                ? environment_stop
                                : "/data/local/tmp/gothrtos/stop";
    }
    g_stop_file = options.stop_file;

    open_log_file(options.log_file);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGHUP, signal_handler);

    auto finish = [](int status) {
        if (g_log_file != nullptr) {
            std::fclose(g_log_file);
            g_log_file = nullptr;
        }
        return status;
    };

    if (options.fb_probe) {
        return finish(run_fb_probe(options));
    }

    if (options.list_inputs) {
        lua_State *state = luaL_newstate();
        if (state == nullptr) {
            return finish(1);
        }
        luaL_openlibs(state);
        luaL_requiref(state, "gothgfx", luaopen_gothgfx, 1);
        lua_pop(state, 1);
        lua_getglobal(state, "gothgfx");
        lua_getfield(state, -1, "scanInputs");
        if (lua_pcall(state, 0, 1, 0) == LUA_OK && lua_istable(state, -1)) {
            lua_Integer count = static_cast<lua_Integer>(luaL_len(state, -1));
            for (lua_Integer index = 1; index <= count; index++) {
                lua_rawgeti(state, -1, index);
                std::printf("%s\n", lua_tostring(state, -1));
                lua_pop(state, 1);
            }
        }
        lua_close(state);
        return finish(0);
    }

    gothdroid_log("starting %s (root %s)", script.c_str(), root.c_str());
    if (!file_exists(script)) {
        gothdroid_log("cannot find UI script %s", script.c_str());
        return finish(1);
    }

    /* Bring up the real GothRTOS kernel on top of the Android process.  The
     * platform expert owns the scheduler contexts; SystemUI.lua runs as a
     * kernel task with the goth userland API. */
    {
        goth_pe_status_t platform_status = goth_pe_init();
        goth_pe_info_t platform_info;

        if (platform_status != GOTH_PE_STATUS_OK) {
            gothdroid_log("platform init failed (%d)",
                          static_cast<int>(platform_status));
            return finish(1);
        }
        if (goth_pe_get_info(&platform_info) == GOTH_PE_STATUS_OK) {
            gothdroid_log("platform: %s, %s, %u CPU(s)",
                          platform_info.platform_name,
                          platform_info.architecture,
                          static_cast<unsigned>(platform_info.logical_cpu_count));
        }
    }

    goth_kernel_init();
    if (!goth_kernel_is_initialized()) {
        gothdroid_log("kernel init failed");
        return finish(1);
    }

    g_ui_request.options = options;
    g_ui_request.script = script;
    g_ui_request.root = root;

    {
        goth_status_t status =
            goth_task_create(&g_ui_task, g_ui_stack, sizeof(g_ui_stack), 1U,
                             ui_task_entry, nullptr);

        if (status != GOTH_STATUS_OK) {
            gothdroid_log("cannot create UI task (%d)",
                          static_cast<int>(status));
            return finish(1);
        }
    }

    gothdroid_log("kernel ready, UI task %u",
                  static_cast<unsigned>(g_ui_task.id));
    {
        goth_status_t status = goth_um_run(4U);

        if (status != GOTH_STATUS_OK) {
            gothdroid_log("scheduler stopped with status %d",
                          static_cast<int>(status));
        }
    }
    (void)goth_task_destroy(&g_ui_task);

    gothdroid_log("UI finished");
    return finish(g_ui_status);
}
