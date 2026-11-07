ELF_TARGET_64 = bpm

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S)$(UNAME_M),Darwinarm64)
CC 	:= x86_64-elf-gcc
else
CC 	:= gcc
endif

ELF_BIN_DIR_64 = bin
ELF_OBJ_DIR_64 = $(ELF_BIN_DIR_64)/obj

LIB_DIR = sysroot/lib
LIBC = $(LIB_DIR)/blibc.a
CRT0 = $(LIB_DIR)/start.o

# sibling checkouts are used as they are, only our own clones get pulled
BLIBC_REPO = https://github.com/Bleed-Kernel/blibc.git
BLIBC_DIR  ?= $(if $(wildcard ../blibc/include/bleed),../blibc,external/blibc)

COMMON_CFLAGS = \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-Wall -Wextra \
	-O2 \
	-nostdlib \
	-no-pie \
	-m64 \
	-Iinclude \
	-Isysroot/include \
	-Isysroot/include/bleed \
	-nostdinc

LDFLAGS = \
	-static \
	-nostdlib \
	-no-pie \
	-m64 \
	-L$(LIB_DIR)

SRCS = $(shell find src -name '*.c')
ELF_OBJS_64 = $(patsubst src/%.c,$(ELF_OBJ_DIR_64)/%.o,$(SRCS))

all: $(ELF_BIN_DIR_64)/$(ELF_TARGET_64)

$(ELF_BIN_DIR_64)/$(ELF_TARGET_64): $(ELF_OBJS_64) $(LIBC)
	@mkdir -p $(ELF_BIN_DIR_64)
	$(CC) $(LDFLAGS) -o $@ $(CRT0) $(ELF_OBJS_64) $(LIBC) -lgcc

$(ELF_OBJ_DIR_64)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -MMD -MP -c $< -o $@

-include $(ELF_OBJS_64:.o=.d)

$(ELF_OBJS_64): | blibc

# synced every build so the sysroot never goes stale against it
blibc:
	@echo "[BLIBC] Preparing blibc"
	@if [ ! -d "$(BLIBC_DIR)" ]; then \
		git clone $(BLIBC_REPO) $(BLIBC_DIR); \
	elif [ "$(BLIBC_DIR)" = "external/blibc" ]; then \
		cd $(BLIBC_DIR) && git pull --ff-only; \
	fi
	@$(MAKE) -C $(BLIBC_DIR)
	@mkdir -p sysroot
	@cp -Rp $(BLIBC_DIR)/sysroot/* sysroot/

clean:
	rm -rf $(ELF_BIN_DIR_64)

distclean: clean
	rm -rf sysroot external

.PHONY: all blibc clean distclean
