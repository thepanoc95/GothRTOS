# Native side of the GothRTOS Android loader.
#
# Build with the Android NDK:
#
#   cmake -S os/Android -B build/android \
#       -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#       -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29
#   cmake --build build/android
#
# This produces:
#   libgothgfx.so  -- the Lua C module used by SystemUI.lua
#   gothdroid      -- the native runtime: embeds the GothRTOS kernel, runs
#                     SystemUI.lua as a task, and opens fb0/evdev
#
# The same file also works for a host build (used by the tests), where it
# produces a loadable gothgfx module and a desktop gothdroid.

cmake_minimum_required(VERSION 3.22)

# libgothgfx is a Lua C module: it intentionally leaves lua_* undefined and
# resolves them from the host that dlopen()s it.  The NDK otherwise links
# shared libraries with -Wl,--no-undefined.
set(ANDROID_ALLOW_UNDEFINED_SYMBOLS ON)

project(gothdroid C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lua")
set(GOTHGFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gothgfx")
set(GOTH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# The Android runtime hosts the real GothRTOS kernel: scheduler, IPC,
# syscalls and the userland API.  Only the platform expert differs per port;
# Android cannot use makecontext/swapcontext (bionic has neither), so
# pexpert/android/pe_android.c implements the same contract with pthreads.
#
# GOTHDROID_PLATFORM=android forces the pthread expert on any host, which is
# useful for testing that scheduler path without a device.
set(GOTHDROID_PLATFORM "auto" CACHE STRING
    "Platform expert to use: auto, android, linux or windows")

if(GOTHDROID_PLATFORM STREQUAL "android")
    set(GOTH_PLATFORM_SOURCE ${GOTH_SRC}/pexpert/android/pe_android.c)
elseif(GOTHDROID_PLATFORM STREQUAL "linux")
    set(GOTH_PLATFORM_SOURCE ${GOTH_SRC}/pexpert/um/pe_linux.c)
elseif(GOTHDROID_PLATFORM STREQUAL "windows")
    set(GOTH_PLATFORM_SOURCE ${GOTH_SRC}/pexpert/um/pe_w64.c)
elseif(ANDROID)
    set(GOTH_PLATFORM_SOURCE ${GOTH_SRC}/pexpert/android/pe_android.c)
elseif(WIN32)
    set(GOTH_PLATFORM_SOURCE ${GOTH_SRC}/pexpert/um/pe_w64.c)
else()
    set(GOTH_PLATFORM_SOURCE ${GOTH_SRC}/pexpert/um/pe_linux.c)
endif()

set(GOTH_KERNEL_SOURCES
    ${GOTH_SRC}/rtos/kernel.c
    ${GOTH_SRC}/rtos/ipc.c
    ${GOTH_SRC}/rtos/syscall.c
    ${GOTH_SRC}/rtos/userapi.c
    ${GOTH_SRC}/pexpert/common.c
    ${GOTH_SRC}/pexpert/um/um_internal.h
    ${GOTH_SRC}/pexpert/pe_internal.h
    ${GOTH_PLATFORM_SOURCE}
    ${GOTH_SRC}/os/init/gothlib.c
    ${GOTH_SRC}/include/gothrtos/kernel.h
    ${GOTH_SRC}/include/gothrtos/syscall.h
    ${GOTH_SRC}/include/gothrtos/userapi.h
    ${GOTH_SRC}/include/gothrtos/pexpert.h
    ${GOTH_SRC}/include/gothrtos/um.h
    ${GOTH_SRC}/include/gothrtos/gothlib.h
)

set(LUA_SOURCES
    ${LUA_DIR}/lapi.c
    ${LUA_DIR}/lcode.c
    ${LUA_DIR}/lctype.c
    ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c
    ${LUA_DIR}/ldump.c
    ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c
    ${LUA_DIR}/llex.c
    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/lobject.c
    ${LUA_DIR}/lopcodes.c
    ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c
    ${LUA_DIR}/lstring.c
    ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltm.c
    ${LUA_DIR}/lundump.c
    ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c
    ${LUA_DIR}/lauxlib.c
    ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/lcorolib.c
    ${LUA_DIR}/ldblib.c
    ${LUA_DIR}/liolib.c
    ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/loslib.c
    ${LUA_DIR}/ltablib.c
    ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/lutf8lib.c
    ${LUA_DIR}/loadlib.c
    ${LUA_DIR}/linit.c
)

set(GOTHGFX_SOURCES
    ${GOTHGFX_DIR}/gothgfx.c
)

set(GOTHGFX_HEADERS
    ${GOTHGFX_DIR}/gothgfx.h
    ${GOTHGFX_DIR}/gothgfx_font.h
)

# ---------------------------------------------------------------------------
# libgothgfx.so -- a normal Lua C module.
# ---------------------------------------------------------------------------
add_library(gothgfx SHARED ${GOTHGFX_SOURCES} ${GOTHGFX_HEADERS})
target_include_directories(gothgfx PRIVATE ${LUA_DIR} ${GOTHGFX_DIR})
target_compile_options(gothgfx PRIVATE -Wall -Wextra)

# The module deliberately leaves lua_* symbols undefined: the Lua host that
# dlopen()s it provides them.  Android's linker needs to be told that this is
# expected, and Lua modules conventionally use the "luaopen_" entry point.
if(NOT MSVC)
    target_link_options(gothgfx PRIVATE -Wl,--allow-shlib-undefined)
endif()

# ---------------------------------------------------------------------------
# gothdroid -- embedded Lua runtime for the framebuffer UI.
# ---------------------------------------------------------------------------
add_executable(gothdroid
    AndroidIO.cpp
    ${GOTH_KERNEL_SOURCES}
    ${GOTHGFX_SOURCES}
    ${GOTHGFX_HEADERS}
    ${LUA_SOURCES}
    ${LUA_DIR}/lua.h
    ${LUA_DIR}/lauxlib.h
    ${LUA_DIR}/lualib.h
)

target_include_directories(gothdroid PRIVATE
    ${LUA_DIR}
    ${GOTHGFX_DIR}
    ${GOTH_SRC}/include
    ${GOTH_SRC}/pexpert
    ${GOTH_SRC}/pexpert/um)

find_package(Threads REQUIRED)

target_compile_definitions(gothdroid PRIVATE LUA_USE_POSIX)
target_compile_options(gothdroid PRIVATE -Wall -Wextra)
target_link_libraries(gothdroid PRIVATE m dl Threads::Threads)

if(ANDROID)
    target_link_libraries(gothdroid PRIVATE log)
endif()

# Optional freestanding image.  The Android NDK's normal PIE executable is the
# supported path; this is only for experiments with a custom loader.
if(GOTHDROID_FREESTANDING)
    target_link_options(gothdroid PRIVATE
        -nostdlib -nostartfiles
        -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
endif()

# Install the UI/userland next to the runtime so `make install` style layouts
# on a device or an emulator image are self-contained.
install(TARGETS gothdroid gothgfx
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
install(DIRECTORY SystemUI DESTINATION share/gothrtos)
install(DIRECTORY devices DESTINATION share/gothrtos)
install(FILES SurfaceLinger.lua DESTINATION share/gothrtos)
