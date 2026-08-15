# Free PC-9821 firmware.
#
#   make MODEL=xa7          build build/xa7/pc98-free-xa7.bin
#   make MODEL=rvii26       build build/rvii26/pc98-free-rvii26.bin
#   make every              build every model
#   make clean
#
# Toolchain is stock host gcc in 16-bit mode — the same recipe the
# QEMU PC-98 self-test suite builds its C tests with.  No cross-compiler
# required.
#
# SPDX-License-Identifier: MIT

MODELS      := xa7 rvii26
MODEL       ?= xa7

ifeq ($(filter $(MODEL),$(MODELS)),)
$(error MODEL=$(MODEL) unknown; choose one of: $(MODELS))
endif

include config/$(MODEL).mk

BUILD       := build/$(MODEL)
TOP         := $(CURDIR)

# Anything that changes how a translation unit is compiled.  Objects
# depend on these, so editing a config or a flag rebuilds rather than
# leaving a stale image behind with no warning.
CONFIGDEPS  := Makefile config/$(MODEL).mk

CC          := gcc
LD          := ld
OBJCOPY     := objcopy
PYTHON      := python3

# -m16 emits 16-bit code but keeps the i386 ABI (32-bit int, 32-bit
# call).  The freestanding flags match the self-test build; the
# jump-table and min-pagesize ones are load-bearing, not cargo cult:
# gcc's jump tables and its pointer-provenance warnings both misbehave
# against 16-bit segmented addresses.
#
# -fno-delete-null-pointer-checks: BANK7 links at 0 (the BIOS runs with
# CS = FD80, so its IPs start there), which makes address 0 a VALID
# address.  gcc otherwise assumes an object's address is never null and
# prunes checks on that basis.  NULL cannot mean "no pointer" in bank 7.
# -std pinned: gcc's default moved to gnu23, where `bool` became a
# keyword and our typedef stopped compiling.  Firmware wants a fixed
# language version anyway — an unannounced dialect change should not be
# able to alter what lands in the ROM.
CFLAGS      := -m16 -march=$(ARCH) -Os -std=gnu11 \
               -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
               -fno-asynchronous-unwind-tables -fomit-frame-pointer \
               -fno-builtin -fno-jump-tables --param=min-pagesize=0 \
               -fno-delete-null-pointer-checks \
               -Wall -Wextra -Wno-unused-parameter \
               -I$(TOP)/include $(addprefix -D,$(CONFIG))

ASFLAGS     := $(CFLAGS)
LDFLAGS     := -m elf_i386 --build-id=none

# ---- BANK4: the ITF ----
ITF_SRCS    := core/post/entry.S \
               core/post/early.S \
               core/post/handoff.S \
               hal/$(HAL_DIR)/early.S \
               hal/$(HAL_DIR)/chipset.c \
               core/post/itf.c \
               core/pci/pci.c \
               core/lib/hwinit.c \
               core/lib/sdip.c \
               core/lib/serial.c
ITF_OBJS    := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(ITF_SRCS))))

# ---- BANK7: the runtime BIOS ----
BIOS_SRCS   := core/bios/entry.S \
               core/bios/vectors.S \
               core/bios/bootjmp.S \
               core/bios/xmove.S \
               core/bios/caps.S \
               core/bios/mps.S \
               core/bios/bios.c \
               core/bios/crt.c \
               core/bios/int18.c \
               core/bios/keytab.c \
               core/bios/misc.c \
               core/bios/disk.c \
               core/boot/optrom.c \
               core/boot/optcall.S \
               core/boot/diskcall.S \
               core/pci/pci.c \
               core/pci/pcibios.c \
               hal/$(HAL_DIR)/ide.c \
               hal/$(HAL_DIR)/chipset.c \
               core/lib/hwinit.c \
               core/lib/sdip.c \
               core/lib/serial.c
BIOS_OBJS   := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(BIOS_SRCS))))

# BANK6 -- the F0000 window.  Carries only the structures an OS scans
# for and the code they name; see banks/bank6.ld.in.
BANK6_SRCS  := core/pci/bios32.S \
               core/bios/pnp.S
BANK6_OBJS  := $(addprefix $(BUILD)/,$(addsuffix .o,$(basename $(BANK6_SRCS))))

IMAGE       := $(BUILD)/$(ROM_NAME)

.PHONY: all every clean distclean help font wadump
all: $(IMAGE)

every:
	@for m in $(MODELS); do $(MAKE) --no-print-directory MODEL=$$m || exit 1; done

BLOBS := $(BUILD)/bank4.code.bin $(BUILD)/bank4.vec.bin \
         $(BUILD)/bank6.code.bin \
         $(BUILD)/bank7.code.bin $(BUILD)/bank7.warm.bin \
         $(BUILD)/bank7.vec.bin

$(IMAGE): $(BLOBS) tools/mkflash.py
	@$(PYTHON) tools/mkflash.py -o $@ \
	    --bank 4=$(BUILD)/bank4.code.bin@0x4000 \
	    --bank 4=$(BUILD)/bank4.vec.bin@0x7ff0 \
	    --bank 6=$(BUILD)/bank6.code.bin@0x4c40 \
	    --bank 7=$(BUILD)/bank7.code.bin@0x0e80 \
	    --bank 7=$(BUILD)/bank7.warm.bin@0x5800 \
	    --bank 7=$(BUILD)/bank7.vec.bin@0x7ff0

$(BUILD)/bank4.elf: $(ITF_OBJS) $(BUILD)/bank4.ld
	@$(LD) $(LDFLAGS) -T $(BUILD)/bank4.ld -o $@ $(ITF_OBJS)

$(BUILD)/bank6.elf: $(BANK6_OBJS) $(BUILD)/bank6.ld
	@$(LD) $(LDFLAGS) -T $(BUILD)/bank6.ld -o $@ $(BANK6_OBJS)

$(BUILD)/bank7.elf: $(BIOS_OBJS) $(BUILD)/bank7.ld
	@$(LD) $(LDFLAGS) -T $(BUILD)/bank7.ld -o $@ $(BIOS_OBJS)

# Code and reset vector are extracted separately rather than as one
# blob spanning the gap between them: that keeps the unused middle of
# the bank at the flash erased state (0xFF) instead of objcopy's zero
# padding, and makes mkflash's bank-usage figures mean something.
$(BUILD)/bank6.code.bin: $(BUILD)/bank6.elf
	@$(OBJCOPY) -O binary -j .bios32 -j .pnp -j .pnpentry $< $@

$(BUILD)/%.code.bin: $(BUILD)/%.elf
	@$(OBJCOPY) -O binary -j .caps -j .text -j .rodata -j .mps -j .data $< $@

$(BUILD)/%.warm.bin: $(BUILD)/%.elf
	@$(OBJCOPY) -O binary -j .warment $< $@

$(BUILD)/%.vec.bin: $(BUILD)/%.elf
	@$(OBJCOPY) -O binary -j .resetvec $< $@

$(BUILD)/%.o: %.c $(CONFIGDEPS)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/%.o: %.S $(CONFIGDEPS)
	@mkdir -p $(dir $@)
	@$(CC) $(ASFLAGS) -MMD -MP -c -o $@ $<

# The linker scripts run through cpp so they share layout.h with the C
# and asm sources: one definition of every address, no chance of a
# script going stale against the header.
$(BUILD)/%.ld: banks/%.ld.in include/pc98/layout.h $(CONFIGDEPS)
	@mkdir -p $(dir $@)
	@$(CC) -E -P -x c -I$(TOP)/include $< -o $@

# ---- character generator ROM (model-independent) ----
#
# Sources are fetched, not vendored: unifont_jp is 9 MiB of BDF.  Both
# are freely licensed — Unifont JP is OFL-1.1 / GPLv2+ with the font
# embedding exception (its kanji are public-domain Izumi glyphs), and
# the IchigoJam 8x8 face is MIT.
#
# Both are pinned — Unifont by release version, IchigoJam by commit,
# because that repository has no tags and a branch name is not a
# version — and both are checked against a recorded SHA-256 before
# anything is built from them.  A character generator that silently
# changes between builds is a font ROM nobody can reproduce.

UNIFONT_VER := 17.0.05
UNIFONT_URL := https://ftp.gnu.org/gnu/unifont/unifont-$(UNIFONT_VER)
UNIFONT_SHA := 044463a47a5b320a1281dcd15fcb3010d6a4ec19603e4193bf28d12909cd009c

ICHIGOJAM_REV := ede57e705c9ae419a8a3d362e464e839d077ae62
ICHIGOJAM_URL := https://raw.githubusercontent.com/IchigoJam/font/$(ICHIGOJAM_REV)
ICHIGOJAM_SHA := 2ebfbd11f0ced56213ac3c24ee7f5479d2eef26734d99d51a1fff7748d600ddd

FONTSRC     := build/fontsrc
FONT        := build/pc98-free-font.rom

# Verify a fetched file, and delete it if it does not match: a bad
# download left on disk would be treated as up to date by the next run.
define checksha
	@got=$$(sha256sum <$(1) | cut -d' ' -f1); \
	if [ "$$got" != "$(2)" ]; then \
	    rm -f $(1); \
	    echo "$(1): SHA-256 mismatch, refusing to build from it" >&2; \
	    echo "  expected $(2)" >&2; \
	    echo "  got      $$got" >&2; \
	    exit 1; \
	fi
endef

.PHONY: font
font: $(FONT)

$(FONTSRC)/unifont_jp.bdf:
	@mkdir -p $(FONTSRC)
	@echo "  FETCH   unifont_jp-$(UNIFONT_VER).bdf.gz"
	@curl -sSL --max-time 300 -o $@.gz \
	    $(UNIFONT_URL)/unifont_jp-$(UNIFONT_VER).bdf.gz
	@gunzip -f $@.gz
	$(call checksha,$@,$(UNIFONT_SHA))

$(FONTSRC)/ichigojam-font.bin:
	@mkdir -p $(FONTSRC)
	@echo "  FETCH   ichigojam-font.bin"
	@curl -sSL --max-time 60 -o $@ $(ICHIGOJAM_URL)/ichigojam-font.bin
	$(call checksha,$@,$(ICHIGOJAM_SHA))

$(FONT): tools/mkfont.py $(FONTSRC)/unifont_jp.bdf $(FONTSRC)/ichigojam-font.bin
	@$(PYTHON) tools/mkfont.py \
	    --unifont-jp $(FONTSRC)/unifont_jp.bdf \
	    --ichigojam $(FONTSRC)/ichigojam-font.bin -o $@

# ---- the work-area oracle (model-independent) ----
#
# A boot sector that dumps the IVT, the work area and the memory
# switches over the serial line.  Run it under the NEC ROM to capture a
# reference and under ours to diff against it -- that comparison is what
# every measured constant in the work area rests on, so it has to be
# buildable, not just described.  NASM, because it is a flat binary with
# a hand-laid-out IPL header.

NASM        := nasm
WADUMP      := build/wadump.bin

.PHONY: wadump
wadump: $(WADUMP)

$(WADUMP): test/wadump.asm
	@mkdir -p $(dir $@)
	@$(NASM) -f bin -o $@ $<
	@echo "$@: $$(stat -c %s $@) bytes"

# Keeps the fetched font sources; use distclean to drop those too.
clean:
	@find build -mindepth 1 -maxdepth 1 ! -name fontsrc -exec rm -rf {} + \
	    2>/dev/null || true

distclean:
	@rm -rf build

help:
	@echo "make [MODEL=<$(MODELS)>]   build one model"
	@echo "make every                 build all models"
	@echo "make font                  build the character generator ROM"
	@echo "make wadump                build the work-area oracle boot sector"
	@echo "make clean / distclean"

# Only this model's dependency files: a build of one model has no
# business reasoning about another model's objects.
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
