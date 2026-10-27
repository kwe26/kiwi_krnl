# Kiwi - a tiny Unix-like kernel with heavy memory compression and a
# JavaScript userland (QuickJS).
#
#   make            build kernel, libc, programs and the initrd
#   make run        boot in QEMU (VGA window + serial on this terminal)
#   make run-nox    boot in QEMU with the serial console only
#   make check      syntax-check every JS file with a host build of kjs
#
# programs/*.c         -> /bin/<name>   (one C file = one program)
# programs/<dir>/*.c   -> /bin/<dir>    (multi-file C program, e.g. kjs)
# programs/*.js        -> /bin/<name>   (JS program run via #!/bin/kjs)
# jslib/*.js           -> /lib/js/      (JS system libraries, jslib/apps: GUI apps)
# rootfs/              -> /             (config and data files)

CC      ?= gcc
CC      := $(if $(filter cc,$(CC)),gcc,$(CC))
LD      := ld
HOSTCC  := gcc
QEMU    ?= qemu-system-i386
B       := build

QJS_VERSION := 2026-06-04
QJS_DIR := third_party/quickjs
QJS_SRC := quickjs.c libregexp.c libunicode.c cutils.c dtoa.c

COMMON_FLAGS := -m32 -std=gnu11 -O2 -g -nostdinc -fno-pie -fno-pic \
                -fno-stack-protector -fcf-protection=none -U_FORTIFY_SOURCE \
                -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
                -Iinclude -Icommon -MMD -MP
KCFLAGS := $(COMMON_FLAGS) -ffreestanding -fno-builtin -mgeneral-regs-only \
           -Ikernel -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
UCFLAGS := $(COMMON_FLAGS) -D__kiwi__ -fno-omit-frame-pointer -Wall \
           -Wno-unused-parameter
QJSFLAGS := $(COMMON_FLAGS) -D__kiwi__ -DCONFIG_VERSION=\"$(QJS_VERSION)\" -w
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

KERNEL_SRC := $(wildcard kernel/*.c) common/lz.c common/huff.c common/alloc.c
KERNEL_OBJ := $(patsubst %.c,$(B)/k/%.o,$(KERNEL_SRC)) $(B)/k/kernel/boot.o

LIBC_SRC := $(wildcard lib/*.c) common/alloc.c
LIBC_OBJ := $(patsubst %.c,$(B)/u/%.o,$(LIBC_SRC)) $(B)/u/lib/setjmp.o
CRT0     := $(B)/u/lib/crt0.o
LIBC     := $(B)/libc.a

QJS_OBJ  := $(patsubst %.c,$(B)/qjs/%.o,$(QJS_SRC))

C_PROGS   := $(basename $(notdir $(wildcard programs/*.c)))
DIR_PROGS := $(notdir $(patsubst %/,%,$(dir $(wildcard programs/*/*.c))))
JS_PROGS  := $(basename $(notdir $(wildcard programs/*.js)))
JS_LIBS   := $(notdir $(wildcard jslib/*.js)) $(patsubst jslib/%,%,$(wildcard jslib/*/*.js))
ROOT_FILES := $(shell find rootfs -type f 2>/dev/null)

BIN := $(B)/root/bin
ALL_BINS := $(addprefix $(BIN)/,$(C_PROGS) $(sort $(DIR_PROGS)) $(JS_PROGS)) \
            $(addprefix $(B)/root/lib/js/,$(JS_LIBS)) \
            $(patsubst rootfs/%,$(B)/root/%,$(ROOT_FILES))

.PHONY: all kernel run run-nox check clean
all: $(B)/kiwi.elf $(B)/initrd.img

kernel: $(B)/kiwi.elf

# ---- kernel ----
$(B)/k/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

$(B)/k/kernel/boot.o: kernel/boot.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

$(B)/kiwi.elf: $(KERNEL_OBJ) kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld -o $@ $(KERNEL_OBJ) $(LIBGCC)

# ---- libc ----
$(B)/u/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(B)/u/lib/%.o: lib/%.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

$(LIBC): $(LIBC_OBJ)
	rm -f $@
	ar rcs $@ $^

# ---- QuickJS (engine core only) ----
$(B)/qjs/%.o: $(QJS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(QJSFLAGS) -c $< -o $@

USER_LINK = $(LD) -m elf_i386 -T lib/user.ld -o $@ $(CRT0) $^ $(LIBC) $(LIBGCC)

# ---- programs ----
$(BIN)/%: $(B)/u/programs/%.o $(CRT0) $(LIBC) lib/user.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -T lib/user.ld -o $@ $(CRT0) $< $(LIBC) $(LIBGCC)
	strip $@

define DIR_PROG
$(BIN)/$(1): $(patsubst %.c,$(B)/u/%.o,$(wildcard programs/$(1)/*.c)) \
             $(if $(filter kjs,$(1)),$(QJS_OBJ)) $(CRT0) $(LIBC) lib/user.ld
	@mkdir -p $$(dir $$@)
	$(LD) -m elf_i386 -T lib/user.ld -o $$@ $(CRT0) \
	    $(patsubst %.c,$(B)/u/%.o,$(wildcard programs/$(1)/*.c)) \
	    $(if $(filter kjs,$(1)),$(QJS_OBJ)) $(LIBC) $(LIBGCC)
	strip $$@
endef
$(foreach p,$(sort $(DIR_PROGS)),$(eval $(call DIR_PROG,$(p))))

# JS programs: installed with a #!/bin/kjs line, syntax-checked on the host
$(BIN)/%: programs/%.js $(B)/host/kjs
	@mkdir -p $(dir $@)
	$(B)/host/kjs --check $<
	@(head -1 $< | grep -q '^#!' || echo '#!/bin/kjs'; cat $<) > $@

$(B)/root/lib/js/%.js: jslib/%.js $(B)/host/kjs
	@mkdir -p $(dir $@)
	$(B)/host/kjs --check $<
	cp $< $@

$(B)/root/%: rootfs/%
	@mkdir -p $(dir $@)
	cp $< $@

# ---- host tools ----
$(B)/host/mkinitrd: tools/mkinitrd.c common/lz.c common/lz.h common/huff.c common/huff.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Wall -Icommon -o $@ tools/mkinitrd.c common/lz.c common/huff.c

HOST_QJS := $(addprefix $(QJS_DIR)/,$(QJS_SRC))
$(B)/host/kjs: $(wildcard programs/kjs/*.c) $(HOST_QJS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -O1 -w -DKJS_HOST -DCONFIG_VERSION=\"$(QJS_VERSION)\" -I$(QJS_DIR) \
	    -o $@ $(wildcard programs/kjs/*.c) $(HOST_QJS) -lm -lpthread

# kjs's C sources need the QuickJS headers
$(B)/u/programs/kjs/%.o: UCFLAGS += -I$(QJS_DIR)

$(B)/initrd.img: $(B)/host/mkinitrd $(ALL_BINS)
	$(B)/host/mkinitrd $(B)/root $@

# ---- running ----
QEMU_FLAGS := -m 128M -kernel $(B)/kiwi.elf -initrd $(B)/initrd.img \
              -device isa-debug-exit,iobase=0xf4,iosize=0x04 -no-reboot
KVM := $(shell [ -w /dev/kvm ] && echo -enable-kvm)

run: all
	$(QEMU) $(KVM) $(QEMU_FLAGS) -serial stdio -append "$(APPEND)"

run-nox: all
	$(QEMU) $(KVM) $(QEMU_FLAGS) -nographic -append "$(APPEND)"

check: $(B)/host/kjs
	@for f in programs/*.js jslib/*.js jslib/*/*.js; do $(B)/host/kjs --check $$f || exit 1; done
	@echo "all JS files OK"

clean:
	rm -rf $(B)

-include $(shell find $(B) -name '*.d' 2>/dev/null)
