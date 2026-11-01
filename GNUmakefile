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
LIBC_TEST_DEFINES = -Derrno=gothc_errno -Dmalloc=gothc_malloc -Dcalloc=gothc_calloc -Drealloc=gothc_realloc -Dfree=gothc_free -Dmemcpy=gothc_memcpy -Dmemmove=gothc_memmove -Dmemset=gothc_memset -Dmemcmp=gothc_memcmp -Dstrlen=gothc_strlen -Dstrnlen=gothc_strnlen -Dstrcmp=gothc_strcmp -Dstrncmp=gothc_strncmp -Dstrcpy=gothc_strcpy -Dstrncpy=gothc_strncpy -Dstrcat=gothc_strcat -Dstrchr=gothc_strchr -Dstrrchr=gothc_strrchr -Dstrtol=gothc_strtol -Dstrtoul=gothc_strtoul -Dstrtoll=gothc_strtoll -Dstrtoull=gothc_strtoull -Datoi=gothc_atoi -Datol=gothc_atol -Datoll=gothc_atoll -Dabs=gothc_abs -Dlabs=gothc_labs

UNAME_S := $(shell uname -s 2>/dev/null)

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

SOURCES = rtos/kernel.c rtos/ipc.c rtos/syscall.c pexpert/common.c $(UM_PLATFORM_SOURCE) os/init/95exc.binfmt.c os/loader/fdt.c os/loader/cmdline.c
OBJECTS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
KAPI_OBJECT = $(BUILD_DIR)/libkapi/kapi.o
KLIB_SOURCES = kalloc.c kbarena.c keigen.c kexpr.c kmath.c kmempool.c knhx.c ksa.c kson.c kstring.c ksw.c kthread.c
KLIB_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/klib/%.o,$(KLIB_SOURCES))
LIBC_SOURCES = libc/src/string.c libc/src/stdlib.c
LIBC_OBJECTS = $(patsubst libc/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_SOURCES))
LUA_SOURCES = lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loslib.c ltablib.c lstrlib.c lutf8lib.c loadlib.c linit.c
LUA_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/luaobj/%.o,$(LUA_SOURCES))
LIBRARY = $(BUILD_DIR)/libgothrtos.a
UM_PLATFORM_STAMP = $(BUILD_DIR)/.um_platform_$(UM_PLATFORM)
KAPI_LIBRARY = $(BUILD_DIR)/libkapi.a
KLIB_LIBRARY = $(BUILD_DIR)/libklib.a
LIBC_LIBRARY = $(BUILD_DIR)/libgothc.a
LUA_LIBRARY = $(BUILD_DIR)/liblua.a
LUA_INTERPRETER_BINARY = $(BUILD_DIR)/lua
LUAEXEC_OBJECT = $(BUILD_DIR)/os/init/luaexec.o
LUAEXEC_TEST_BINARY = $(BUILD_DIR)/test_luaexec
BIN_FMT_TEST_BINARY = $(BUILD_DIR)/test_95exc
FDT_TEST_BINARY = $(BUILD_DIR)/test_fdt
CMDLINE_TEST_BINARY = $(BUILD_DIR)/test_cmdline
KERNEL_BINARY = $(BUILD_DIR)/kern.ldr
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

.PHONY: all test clean kernel lua

all: $(LIBRARY) $(KAPI_LIBRARY) $(KLIB_LIBRARY) $(LIBC_LIBRARY) $(LUA_LIBRARY) $(LUA_INTERPRETER_BINARY) $(LUAEXEC_OBJECT) $(KERNEL_BINARY)

kernel: $(KERNEL_BINARY)

lua: $(LUA_INTERPRETER_BINARY)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c include/gothrtos/kernel.h include/gothrtos/syscall.h include/gothrtos/um.h include/gothrtos/pexpert.h include/gothrtos/95exc.h include/gothrtos/fdt.h include/gothrtos/cmdline.h pexpert/pe_internal.h pexpert/um/um_internal.h | $(BUILD_DIR)
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

$(KERNEL_BINARY): os/loader/kern.c $(LIBRARY) $(LUAEXEC_OBJECT) $(LUA_LIBRARY) include/gothrtos/cmdline.h include/gothrtos/kernel.h include/gothrtos/luaexec.h include/gothrtos/pexpert.h include/gothrtos/um.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) os/loader/kern.c $(LUAEXEC_OBJECT) -L$(BUILD_DIR) -lgothrtos -llua $(LUA_LIBS) $(UM_PLATFORM_LIBS) -o $@

$(KAPI_OBJECT): libkapi/kapi.c include/gothrtos/kernel.h include/gothrtos/syscall.h include/gothrtos/kapi.h | $(BUILD_DIR)
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

$(BUILD_DIR)/libc/%.o: libc/src/%.c libc/include/string.h libc/include/stdlib.h libc/include/errno.h libc/include/limits.h include/gothrtos/libc.h | $(BUILD_DIR)
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

$(LUA_INTERPRETER_BINARY): os/lua/lua.c $(LUA_LIBRARY) os/lua/lua.h os/lua/lauxlib.h os/lua/lualib.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(LUA_CPPFLAGS) $(CFLAGS) $(LUA_CFLAGS) os/lua/lua.c -L$(BUILD_DIR) -llua $(LUA_LIBS) -o $@

$(LUAEXEC_OBJECT): os/init/luaexec.c include/gothrtos/luaexec.h os/lua/lua.h os/lua/lauxlib.h os/lua/lualib.h | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@printf "  $(COLOR_CC)%-12s$(RESET) %s\n" "CC" "$<"
	$(CC) $(LUA_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_BINARY): tests/test_kernel.c $(LIBRARY) $(KAPI_LIBRARY) include/gothrtos/kernel.h include/gothrtos/syscall.h include/gothrtos/kapi.h include/gothrtos/um.h include/gothrtos/pexpert.h | $(BUILD_DIR)
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

$(LUAEXEC_TEST_BINARY): tests/test_luaexec.c tests/lua_smoke.lua $(LUAEXEC_OBJECT) $(LUA_LIBRARY) include/gothrtos/luaexec.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(LUA_CPPFLAGS) $(CFLAGS) tests/test_luaexec.c $(LUAEXEC_OBJECT) -L$(BUILD_DIR) -llua $(LUA_LIBS) -o $@

$(BIN_FMT_TEST_BINARY): tests/test_95exc.c $(LIBRARY) include/gothrtos/95exc.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_95exc.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(FDT_TEST_BINARY): tests/test_fdt.c $(LIBRARY) include/gothrtos/fdt.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_fdt.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

$(CMDLINE_TEST_BINARY): tests/test_cmdline.c $(LIBRARY) include/gothrtos/cmdline.h | $(BUILD_DIR)
	@printf "  $(COLOR_LD)%-12s$(RESET) %s\n" "LD" "$@"
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_cmdline.c -L$(BUILD_DIR) -lgothrtos $(UM_PLATFORM_LIBS) -o $@

test: $(TEST_BINARY) $(KLIB_TEST_BINARY) $(LIBC_TEST_BINARY) $(LUAEXEC_TEST_BINARY) $(BIN_FMT_TEST_BINARY) $(FDT_TEST_BINARY) $(CMDLINE_TEST_BINARY) $(KERNEL_BINARY) $(LUA_INTERPRETER_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running kernel tests..."
	$(TEST_BINARY)
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running klib tests..."
	$(KLIB_TEST_BINARY)
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
	@printf "  $(COLOR_TEST)%-12s$(RESET) %s\n" "TEST" "Running system runtime smoke tests..."
	$(KERNEL_BINARY) -kern_sym=/kern.symbols -verbose=0 -root=/img/root.bin -mem=dynamic -drv_root=/kern.builts
	$(KERNEL_BINARY) -init=tests/test_system.lua -verbose=0
	$(KERNEL_BINARY) -init=os/init/init.d/init.lua -root=/img/root.bin -drv_root=/kern.builts -mem=dynamic -verbose=0
	$(LUA_INTERPRETER_BINARY) tests/test_system.lua
	$(LUA_INTERPRETER_BINARY) tests/test_init_modules.lua

clean:
	@printf "  $(COLOR_RM)%-12s$(RESET) %s\n" "RM" "$(BUILD_DIR)"
	rm -rf $(BUILD_DIR)
