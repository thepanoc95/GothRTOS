CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -static
CPPFLAGS ?= -Iinclude -Ipexpert -Ipexpert/um -static
BUILD_DIR ?= build
KLIB_CPPFLAGS ?= -Ilib -static
KLIB_CFLAGS ?= -std=gnu11 -O2 -pthread -static
KLIB_LIBS ?= -lm -pthread
LIBC_CPPFLAGS ?= -Ilibc/include -Iinclude -static
LIBC_CFLAGS ?= -std=c11 -ffreestanding -fno-builtin -static
LUA_CPPFLAGS ?= -Ios/lua -Iinclude
LUA_CFLAGS ?= -std=c11 -O2 -static
LUA_LIBS ?= -lm
GOTHC_LUA_CPPFLAGS ?= -Ilibc/include -Ios/lua -Iinclude -static
GOTHC_LUA_CFLAGS ?= -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pie -static
GOTHC_LUA_LDFLAGS ?= -nostdlib -nostartfiles -static -no-pie
LIBC_TEST_DEFINES = -Derrno=gothc_errno -Dmalloc=gothc_malloc -Dcalloc=gothc_calloc -Drealloc=gothc_realloc -Dfree=gothc_free -Dmemcpy=gothc_memcpy -Dmemmove=gothc_memmove -Dmemset=gothc_memset -Dmemcmp=gothc_memcmp -Dstrlen=gothc_strlen -Dstrnlen=gothc_strnlen -Dstrcmp=gothc_strcmp -Dstrncmp=gothc_strncmp -Dstrcpy=gothc_strcpy -Dstrncpy=gothc_strncpy -Dstrcat=gothc_strcat -Dstrchr=gothc_strchr -Dstrrchr=gothc_strrchr -Dstrtol=gothc_strtol -Dstrtoul=gothc_strtoul -Dstrtoll=gothc_strtoll -Dstrtoull=gothc_strtoull -Datoi=gothc_atoi -Datol=gothc_atol -Datoll=gothc_atoll -Dabs=gothc_abs -Dlabs=gothc_labs -Dllabs=gothc_llabs

UNAME_S := $(shell uname -s 2>/dev/null)

# KLib currently contains x86-specific code (including SSE2 intrinsics).
# GothRTOS does not use KLib on ARM targets, so detect the compiler target
# rather than the machine running make (this also handles cross-compilers).
CC_TARGET := $(shell $(CC) -dumpmachine 2>/dev/null)
ifneq ($(filter aarch64% arm64%,$(CC_TARGET)),)
  GOTHC_START_SOURCE ?= libc/src/start_aarch64.S
else ifneq ($(filter x86_64% amd64%,$(CC_TARGET)),)
  GOTHC_START_SOURCE ?= libc/src/start_x86_64.S
else
  GOTHC_START_SOURCE ?=
endif
ifneq ($(filter aarch64% arm%,$(CC_TARGET)),)
  KLIB_ENABLED ?= no
else
  KLIB_ENABLED ?= yes
endif

ifeq ($(UM_PLATFORM),)
  ifeq ($(OS),Windows_NT)
    UM_PLATFORM := windows
  else ifeq ($(UNAME_S),Linux)
    UM_PLATFORM := linux
  else ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
    UM_PLATFORM := windows
  else
    $(error unable to detect host OS; set UM_PLATFORM=linux or windows)
  endif
endif

ifeq ($(UM_PLATFORM),linux)
UM_PLATFORM_SOURCE = pexpert/um/pe_linux.c
UM_PLATFORM_LIBS =
else ifeq ($(UM_PLATFORM),windows)
UM_PLATFORM_SOURCE = pexpert/um/pe_w64.c
UM_PLATFORM_LIBS =
else
$(error UM_PLATFORM must be "linux" or "windows")
endif

SOURCES = rtos/kernel.c rtos/sched.c rtos/ipc.c rtos/syscall.c rtos/userapi.c \
	rtos/klog.c rtos/mem.c rtos/vfs.c rtos/nanokernel.c kernel/hdf/hdf.c \
	pexpert/common.c $(UM_PLATFORM_SOURCE) \
	os/init/95exc.binfmt.c os/loader/fdt.c os/loader/cmdline.c \
	os/loader/sparsekernel.c
OBJECTS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
KAPI_OBJECT = $(BUILD_DIR)/libkapi/kapi.o
KLIB_SOURCES = kalloc.c kbarena.c keigen.c kexpr.c kmath.c kmempool.c knhx.c ksa.c kson.c kstring.c ksw.c kthread.c
KLIB_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/klib/%.o,$(KLIB_SOURCES))
LIBC_SOURCES = libc/src/string.c libc/src/stdlib.c libc/src/stdio.c \
	libc/src/convert.c libc/src/process.c libc/src/ctype.c \
	libc/src/locale.c libc/src/math.c libc/src/time.c libc/src/signal.c \
	libc/src/heap.c libc/src/start.c
LIBC_OBJECTS = $(patsubst libc/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_SOURCES))
LIBC_HEADERS = $(wildcard libc/include/*.h) include/gothrtos/libc.h \
	libc/src/goth_syscall.h libc/third_party/stb_sprintf.h
LUA_SOURCES = lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loslib.c ltablib.c lstrlib.c lutf8lib.c loadlib.c linit.c
LUA_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/luaobj/%.o,$(LUA_SOURCES))
GOTHC_LUA_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/gothclua/%.o,$(LUA_SOURCES))
GOTHC_LUA_MAIN_OBJECT = $(BUILD_DIR)/gothclua/lua.o
GOTHC_START_OBJECT = $(patsubst libc/src/%.S,$(BUILD_DIR)/gothclua/%.o,$(GOTHC_START_SOURCE))
LIBRARY = $(BUILD_DIR)/libgothrtos.a
UM_PLATFORM_STAMP = $(BUILD_DIR)/.um_platform_$(UM_PLATFORM)
KAPI_LIBRARY = $(BUILD_DIR)/libkapi.a
KLIB_LIBRARY = $(BUILD_DIR)/libklib.a
ifeq ($(KLIB_ENABLED),yes)
KLIB_ALL_TARGETS = $(KLIB_LIBRARY)
KLIB_TEST_TARGETS = $(KLIB_TEST_BINARY)
KLIB_TEST_COMMAND = $(KLIB_TEST_BINARY)
else ifeq ($(KLIB_ENABLED),no)
KLIB_ALL_TARGETS =
KLIB_TEST_TARGETS =
KLIB_TEST_COMMAND =
else
$(error KLIB_ENABLED must be "yes" or "no")
endif
LIBC_LIBRARY = $(BUILD_DIR)/libgothc.a
LUA_LIBRARY = $(BUILD_DIR)/liblua.a
GOTHC_LUA_LIBRARY = $(BUILD_DIR)/liblua-gothc.a
LUA_INTERPRETER_BINARY = $(BUILD_DIR)/lua
LUAEXEC_OBJECT = $(BUILD_DIR)/os/init/luaexec.o
GOTHLIB_OBJECT = $(BUILD_DIR)/os/init/gothlib.o
LUAEXEC_TEST_BINARY = $(BUILD_DIR)/test_luaexec
USERAPI_TEST_BINARY = $(BUILD_DIR)/test_userapi
USERAPI_LUA_STAMP = $(BUILD_DIR)/.userapi_lua_stamp
BIN_FMT_TEST_BINARY = $(BUILD_DIR)/test_95exc
FDT_TEST_BINARY = $(BUILD_DIR)/test_fdt
CMDLINE_TEST_BINARY = $(BUILD_DIR)/test_cmdline
HDF_TEST_BINARY = $(BUILD_DIR)/test_hdf
SPARSE_TEST_BINARY = $(BUILD_DIR)/test_sparsekernel
SYSGOTH_TEST_BINARY = $(BUILD_DIR)/test_sysgoth
SPARSEKERNEL_GEN = $(BUILD_DIR)/sparsekernel-gen
KERNEL_BINARY = $(BUILD_DIR)/kern.ldr
ARM_CC ?= arm-none-eabi-gcc
ARM_OBJCOPY ?= arm-none-eabi-objcopy
ARM_BUILD_DIR ?= $(BUILD_DIR)/arm32
ARM_CPPFLAGS ?= -Iinclude -Ipexpert -Ipexpert/arm
ARM_CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic \
	-mcpu=cortex-a15 -marm -mfloat-abi=soft -ffreestanding -fno-builtin \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -O2 -g
ARM_LDFLAGS ?= -nostdlib -nostartfiles -T pexpert/arm/arm32.ld \
	-Wl,--gc-sections -Wl,-Map=$(ARM_BUILD_DIR)/kern.map
ARM_LIBS ?= -lgcc
ARM_C_SOURCES = rtos/kernel.c rtos/sched.c rtos/ipc.c rtos/syscall.c \
	rtos/klog.c rtos/mem.c rtos/vfs.c pexpert/common.c \
	pexpert/arm/pe_arm32.c pexpert/arm/pe_arm32_qemu.c \
	pexpert/arm/arm32_libc.c os/loader/kern_arm.c
ARM_ASM_SOURCES = pexpert/arm/arm32_start.S
ARM_OBJECTS = $(patsubst %.c,$(ARM_BUILD_DIR)/obj/%.o,$(ARM_C_SOURCES)) \
	$(patsubst %.S,$(ARM_BUILD_DIR)/obj/%.o,$(ARM_ASM_SOURCES))
ARM_ELF = $(ARM_BUILD_DIR)/kern.elf
ARM_BIN = $(ARM_BUILD_DIR)/kern.bin
ARM_HEADERS = include/gothrtos/kernel.h include/gothrtos/pexpert.h \
	include/gothrtos/um.h include/gothrtos/arm32.h \
	include/gothrtos/klog.h include/gothrtos/mem.h include/gothrtos/vfs.h \
	rtos/kinternal.h pexpert/pe_internal.h pexpert/arm/um_internal.h pexpert/arm/pe_arm32.h
QEMU_ARM ?= qemu-system-arm
QEMU_ARM_FLAGS ?= -M virt -cpu cortex-a15 -m 128M -nographic \
	-semihosting-config enable=on,target=native

# Raspberry Pi Zero (BCM2835, ARM1176JZF-S / ARMv6K)
RPI_CC ?= arm-none-eabi-gcc
RPI_OBJCOPY ?= arm-none-eabi-objcopy
RPI_BUILD_DIR ?= $(BUILD_DIR)/rpi-zero
RPI_CPPFLAGS ?= -Iinclude -Ipexpert -Ipexpert/arm \
	-DGOTH_ARM32_BCM2835
ifdef GOTH_ARM32_BCM2835_UART_CLOCK
RPI_CPPFLAGS += -DGOTH_ARM32_BCM2835_UART_CLOCK=$(GOTH_ARM32_BCM2835_UART_CLOCK)
endif
RPI_CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic \
	-mcpu=arm1176jzf-s -marm -mfloat-abi=soft -ffreestanding -fno-builtin \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -O2 -g
RPI_LDFLAGS ?= -nostdlib -nostartfiles -T pexpert/arm/bcm2835.ld \
	-Wl,--gc-sections -Wl,-Map=$(RPI_BUILD_DIR)/kernel.map
RPI_LIBS ?= -lgcc
RPI_C_SOURCES = rtos/kernel.c rtos/sched.c rtos/ipc.c rtos/syscall.c \
	rtos/klog.c rtos/mem.c rtos/vfs.c pexpert/common.c \
	pexpert/arm/pe_arm32.c pexpert/arm/pe_bcm2835.c \
	pexpert/arm/arm32_libc.c os/loader/kern_arm.c
RPI_ASM_SOURCES = pexpert/arm/arm32_start.S
RPI_OBJECTS = $(patsubst %.c,$(RPI_BUILD_DIR)/obj/%.o,$(RPI_C_SOURCES)) \
	$(patsubst %.S,$(RPI_BUILD_DIR)/obj/%.o,$(RPI_ASM_SOURCES))
RPI_ELF = $(RPI_BUILD_DIR)/kernel.elf
RPI_IMG = $(RPI_BUILD_DIR)/kernel.img
QEMU_RPI_FLAGS ?= -M raspi0 -cpu arm1176 -m 512M -nographic
LIBC_TEST_OBJECTS = $(BUILD_DIR)/libc_test/string.o $(BUILD_DIR)/libc_test/stdlib.o
TEST_BINARY = $(BUILD_DIR)/test_kernel
KLIB_TEST_BINARY = $(BUILD_DIR)/test_klib
LIBC_TEST_BINARY = $(BUILD_DIR)/test_libc

# --- Pretty Output Configuration ---
COLOR_CC   := \033[1;34m
COLOR_LD   := \033[1;32m
COLOR_AR   := \033[1;35m
COLOR_RM   := \033[1;31m
COLOR_TEST := \033[1;36m
RESET      := \033[0m

# Hide commands globally unless 'V' (Verbose) is requested
ifndef V
.SILENT:
endif

.PHONY: all test clean kernel lua arm32 run-arm32 test-arm32 \
	rpi-zero run-rpi-zero test-rpi-zero

all: $(LIBRARY) $(KAPI_LIBRARY) $(KLIB_ALL_TARGETS) $(LIBC_LIBRARY) $(LUA_LIBRARY) $(LUA_INTERPRETER_BINARY) $(LUAEXEC_OBJECT) $(GOTHLIB_OBJECT) $(KERNEL_BINARY)

kernel: $(KERNEL_BINARY)

lua: $(LUA_INTERPRETER_BINARY)

arm32: $(ARM_ELF) $(ARM_BIN)

run-arm32: $(ARM_ELF)
	$(QEMU_ARM) $(QEMU_ARM_FLAGS) -kernel $(ARM_ELF)

test-arm32: $(ARM_ELF)
	sh tests/run_arm32_qemu.sh $(ARM_ELF) $(ARM_BUILD_DIR)/qemu.log

rpi-zero: $(RPI_ELF) $(RPI_IMG)

run-rpi-zero: $(RPI_ELF)
	$(QEMU_ARM) $(QEMU_RPI_FLAGS) -kernel $(RPI_ELF)

test-rpi-zero: $(RPI_ELF)
	sh tests/run_rpi_qemu.sh $(RPI_ELF) $(RPI_BUILD_DIR)/qemu.log

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(ARM_BUILD_DIR):
	@mkdir -p $(ARM_BUILD_DIR)

$(ARM_BUILD_DIR)/obj/%.o: %.c $(ARM_HEADERS) | $(ARM_BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC(arm32)" "$<"
	$(ARM_CC) $(ARM_CPPFLAGS) $(ARM_CFLAGS) -c $< -o $@

$(ARM_BUILD_DIR)/obj/%.o: %.S $(ARM_HEADERS) | $(ARM_BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "AS(arm32)" "$<"
	$(ARM_CC) $(ARM_CPPFLAGS) $(ARM_CFLAGS) -c $< -o $@

$(ARM_ELF): $(ARM_OBJECTS) pexpert/arm/arm32.ld
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD(arm32)" "$@"
	$(ARM_CC) $(ARM_CFLAGS) $(ARM_LDFLAGS) $(ARM_OBJECTS) $(ARM_LIBS) -o $@

$(ARM_BIN): $(ARM_ELF)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "OBJCOPY" "$@"
	$(ARM_OBJCOPY) -O binary $< $@

$(RPI_BUILD_DIR):
	@mkdir -p $(RPI_BUILD_DIR)

$(RPI_BUILD_DIR)/obj/%.o: %.c $(ARM_HEADERS) | $(RPI_BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC(rpi)" "$<"
	$(RPI_CC) $(RPI_CPPFLAGS) $(RPI_CFLAGS) -c $< -o $@

$(RPI_BUILD_DIR)/obj/%.o: %.S $(ARM_HEADERS) | $(RPI_BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "AS(rpi)" "$<"
	$(RPI_CC) $(RPI_CPPFLAGS) $(RPI_CFLAGS) -c $< -o $@

$(RPI_ELF): $(RPI_OBJECTS) pexpert/arm/bcm2835.ld
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD(rpi)" "$@"
	$(RPI_CC) $(RPI_CFLAGS) $(RPI_LDFLAGS) $(RPI_OBJECTS) $(RPI_LIBS) -o $@

$(RPI_IMG): $(RPI_ELF)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "OBJCOPY" "$@"
	$(RPI_OBJCOPY) -O binary $< $@

$(BUILD_DIR)/%.o: %.c include/gothrtos/kernel.h include/gothrtos/syscall.h include/gothrtos/um.h include/gothrtos/pexpert.h include/gothrtos/95exc.h include/gothrtos/fdt.h include/gothrtos/cmdline.h include/gothrtos/klog.h include/gothrtos/mem.h include/gothrtos/vfs.h include/gothrtos/nanokernel.h include/gothrtos/hdf.h include/gothrtos/sparsekernel.h rtos/kinternal.h pexpert/pe_internal.h pexpert/um/um_internal.h | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(UM_PLATFORM_STAMP): | $(BUILD_DIR)
	@rm -f $(BUILD_DIR)/.um_platform_*
	@touch $@

$(LIBRARY): $(OBJECTS) $(UM_PLATFORM_STAMP)
	@printf "  $(COLOR_AR)%-12s$(RESET) %s\n" "AR" "$@"
	@rm -f $@
	$(AR) rcs $@ $(OBJECTS)

$(KERNEL_BINARY): os/loader/kern.c $(LIBRARY) $(LUAEXEC_OBJECT) $(GOTHLIB_OBJECT) $(LUA_LIBRARY) include/gothrtos/cmdline.h include/gothrtos/kernel.h include/gothrtos/luaexec.h include/gothrtos/gothlib.h include/gothrtos/userapi.h include/gothrtos/pexpert.h include/gothrtos/um.h include/gothrtos/klog.h include/gothrtos/mem.h include/gothrtos/vfs.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) os/loader/kern.c $(LUAEXEC_OBJECT) $(GOTHLIB_OBJECT) -L$(BUILD_DIR) -lgothrtos -llua $(LUA_LIBS) $(UM_PLATFORM_LIBS) -o $@

$(KAPI_OBJECT): libkapi/kapi.c include/gothrtos/kernel.h include/gothrtos/syscall.h include/gothrtos/kapi.h include/gothrtos/vfs.h | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(KAPI_LIBRARY): $(KAPI_OBJECT)
	@printf "  $(COLOR_AR)%-12s$(RESET) %s\n" "AR" "$@"
	$(AR) rcs $@ $^

$(BUILD_DIR)/klib/%.o: lib/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(KLIB_CPPFLAGS) $(KLIB_CFLAGS) -c $< -o $@

$(KLIB_LIBRARY): $(KLIB_OBJECTS)
	@printf "  $(COLOR_AR)%-12s$(RESET) %s\n" "AR" "$@"
	$(AR) rcs $@ $^

$(BUILD_DIR)/libc/%.o: libc/src/%.c $(LIBC_HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(LIBC_CPPFLAGS) $(LIBC_CFLAGS) -c $< -o $@

$(LIBC_LIBRARY): $(LIBC_OBJECTS)
	@printf "  $(COLOR_AR)%-12s$(RESET) %s\n" "AR" "$@"
	$(AR) rcs $@ $^

$(BUILD_DIR)/luaobj/%.o: os/lua/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(LUA_CPPFLAGS) $(LUA_CFLAGS) -c $< -o $@

$(LUA_LIBRARY): $(LUA_OBJECTS)
	@printf "  $(COLOR_AR)%-12s$(RESET) %s\n" "AR" "$@"
	$(AR) rcs $@ $^

$(BUILD_DIR)/gothclua/%.o: os/lua/%.c $(LIBC_HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC(gothc)" "$<"
	$(CC) $(GOTHC_LUA_CPPFLAGS) $(GOTHC_LUA_CFLAGS) -c $< -o $@

$(BUILD_DIR)/gothclua/%.o: libc/src/%.S $(LIBC_HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "AS(gothc)" "$<"
	$(CC) $(GOTHC_LUA_CFLAGS) -c $< -o $@

$(GOTHC_LUA_LIBRARY): $(GOTHC_LUA_OBJECTS)
	@printf "  $(COLOR_AR)%-12s$(RESET) %s\n" "AR" "$@"
	$(AR) rcs $@ $^

$(LUA_INTERPRETER_BINARY): $(GOTHC_LUA_MAIN_OBJECT) $(GOTHC_LUA_LIBRARY) \
		$(LIBC_LIBRARY) $(GOTHC_START_OBJECT) os/lua/lua.h \
		os/lua/lauxlib.h os/lua/lualib.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(GOTHC_LUA_LDFLAGS) $(GOTHC_LUA_MAIN_OBJECT) \
		$(GOTHC_START_OBJECT) -Wl,--start-group -L$(BUILD_DIR) \
		-llua-gothc -lgothc -Wl,--end-group -lgcc -o $@

$(LUAEXEC_OBJECT): os/init/luaexec.c include/gothrtos/luaexec.h os/lua/lua.h os/lua/lauxlib.h os/lua/lualib.h | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(LUA_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(GOTHLIB_OBJECT): os/init/gothlib.c include/gothrtos/gothlib.h include/gothrtos/userapi.h include/gothrtos/klog.h include/gothrtos/mem.h include/gothrtos/vfs.h os/lua/lua.h os/lua/lauxlib.h | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(LUA_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_BINARY): tests/test_kernel.c $(LIBRARY) $(KAPI_LIBRARY) include/gothrtos/kernel.h include/gothrtos/syscall.h include/gothrtos/kapi.h include/gothrtos/um.h include/gothrtos/pexpert.h include/gothrtos/klog.h include/gothrtos/mem.h include/gothrtos/vfs.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_kernel.c -L$(BUILD_DIR) -lkapi -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(KLIB_TEST_BINARY): tests/test_klib.c $(KLIB_LIBRARY) | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(KLIB_CPPFLAGS) $(CFLAGS) $< -L$(BUILD_DIR) -lklib $(KLIB_LIBS) -o $@

$(BUILD_DIR)/libc_test/%.o: libc/src/%.c libc/include/string.h libc/include/stdlib.h libc/include/errno.h libc/include/limits.h include/gothrtos/libc.h | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(LIBC_CPPFLAGS) $(LIBC_CFLAGS) $(LIBC_TEST_DEFINES) -c $< -o $@

$(LIBC_TEST_BINARY): tests/test_libc.c $(LIBC_TEST_OBJECTS) libc/include/string.h libc/include/stdlib.h libc/include/errno.h libc/include/limits.h include/gothrtos/libc.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(LIBC_CPPFLAGS) $(LIBC_CFLAGS) $(LIBC_TEST_DEFINES) $(CFLAGS) $< $(LIBC_TEST_OBJECTS) -o $@

$(LUAEXEC_TEST_BINARY): tests/test_luaexec.c tests/lua_smoke.lua $(LIBRARY) $(LUAEXEC_OBJECT) $(GOTHLIB_OBJECT) $(LUA_LIBRARY) include/gothrtos/luaexec.h include/gothrtos/gothlib.h include/gothrtos/userapi.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(LUA_CPPFLAGS) $(CFLAGS) tests/test_luaexec.c $(LUAEXEC_OBJECT) $(GOTHLIB_OBJECT) -L$(BUILD_DIR) -lgothrtos -llua $(LUA_LIBS) -o $@

$(BIN_FMT_TEST_BINARY): tests/test_95exc.c $(LIBRARY) include/gothrtos/95exc.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_95exc.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(FDT_TEST_BINARY): tests/test_fdt.c $(LIBRARY) include/gothrtos/fdt.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_fdt.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(CMDLINE_TEST_BINARY): tests/test_cmdline.c $(LIBRARY) include/gothrtos/cmdline.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_cmdline.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(HDF_TEST_BINARY): tests/test_hdf.c $(LIBRARY) include/gothrtos/hdf.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_hdf.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(SPARSE_TEST_BINARY): tests/test_sparsekernel.c $(LIBRARY) include/gothrtos/sparsekernel.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_sparsekernel.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(SYSGOTH_TEST_BINARY): tests/test_sysgoth.c sysgoth/sysgoth.c os/loader/sparsekernel.c include/gothrtos/sparsekernel.h sysgoth/sysgoth.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isysgoth tests/test_sysgoth.c sysgoth/sysgoth.c os/loader/sparsekernel.c $(UM_PLATFORM_LIBS) -o $@

$(SPARSEKERNEL_GEN): tools/sparsekernel_gen.c os/loader/sparsekernel.c include/gothrtos/sparsekernel.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tools/sparsekernel_gen.c os/loader/sparsekernel.c $(UM_PLATFORM_LIBS) -o $@

$(USERAPI_TEST_BINARY): tests/test_userapi.c $(LIBRARY) include/gothrtos/userapi.h include/gothrtos/kernel.h include/gothrtos/pexpert.h include/gothrtos/syscall.h include/gothrtos/vfs.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_userapi.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(USERAPI_LUA_STAMP): tests/test_userapi.lua $(KERNEL_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running userapi lua binding tests..."
	$(KERNEL_BINARY) -init=tests/test_userapi.lua -verbose=0

test: $(TEST_BINARY) $(KLIB_TEST_TARGETS) $(LIBC_TEST_BINARY) $(LUAEXEC_TEST_BINARY) $(BIN_FMT_TEST_BINARY) $(FDT_TEST_BINARY) $(CMDLINE_TEST_BINARY) $(HDF_TEST_BINARY) $(SPARSE_TEST_BINARY) $(SYSGOTH_TEST_BINARY) $(SPARSEKERNEL_GEN) $(USERAPI_TEST_BINARY) $(USERAPI_LUA_STAMP) $(KERNEL_BINARY) $(LUA_INTERPRETER_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\\n" "TEST" "Running kernel tests..."
	$(TEST_BINARY)
	$(KLIB_TEST_COMMAND)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running libc tests..."
	$(LIBC_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running luaexec tests..."
	$(LUAEXEC_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running binfmt tests..."
	$(BIN_FMT_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running fdt tests..."
	$(FDT_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running cmdline tests..."
	$(CMDLINE_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running hdf tests..."
	$(HDF_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running sparsekernel tests..."
	$(SPARSE_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running sysgoth tests..."
	$(SYSGOTH_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running userapi tests..."
	$(USERAPI_TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running system runtime smoke tests..."
	$(KERNEL_BINARY) -kern_sym=/kern.symbols -verbose=0 -root=/img/root.bin -mem=dynamic -drv_root=/kern.builts
	$(KERNEL_BINARY) -init=tests/test_system.lua -verbose=0
	$(KERNEL_BINARY) -init=os/init/init.d/init.lua -root=/img/root.bin -drv_root=/kern.builts -mem=dynamic -verbose=0
	$(LUA_INTERPRETER_BINARY) tests/test_system.lua
	$(LUA_INTERPRETER_BINARY) tests/test_init_modules.lua
	$(LUA_INTERPRETER_BINARY) tests/test_userland_api.lua
	$(KERNEL_BINARY) -init=tests/test_userland_api.lua -verbose=0

clean:
	@printf "  $(COLOR_RM)%-12s$(RESET) %s\n" "RM" "$(BUILD_DIR)"
	rm -rf $(BUILD_DIR)
