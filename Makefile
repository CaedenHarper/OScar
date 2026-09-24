SHELL := /bin/sh
.DELETE_ON_ERROR:

BUILD_DIR := build
DEPS_DIR := deps
ISO_ROOT := $(BUILD_DIR)/iso_root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/barebones-kernel.iso
CPP_SOURCES := $(wildcard src/*.cpp)
ASM_SOURCES := $(wildcard src/*.S)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES)) \
	$(patsubst src/%.S,$(BUILD_DIR)/asm/%.o,$(ASM_SOURCES))

CXX := clang++
HOST_CC ?= cc
QEMU ?= qemu-system-x86_64

LIMINE_DIR := $(DEPS_DIR)/limine
PROTOCOL_DIR := $(DEPS_DIR)/limine-protocol
LIMINE_ARCHIVE := $(DEPS_DIR)/limine-binary.tar.gz

CPPFLAGS := -I$(PROTOCOL_DIR)/include
CXXFLAGS := \
	-target x86_64-unknown-none-elf \
	-std=gnu++20 -O2 -g \
	-Wall -Wextra -Wpedantic \
	-ffreestanding -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-stack-check \
	-fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
	-mno-mmx -mno-sse -mno-sse2 \
	-ffunction-sections -fdata-sections
ASFLAGS := \
	-target x86_64-unknown-none-elf \
	-ffreestanding -fno-pie -mno-red-zone
LDFLAGS := \
	-target x86_64-unknown-none-elf -fuse-ld=lld \
	-nostdlib -static \
	-Wl,-T,linker.ld \
	-Wl,--gc-sections \
	-Wl,--build-id=none \
	-Wl,-z,max-page-size=0x1000 \
	-Wl,-z,noexecstack

QEMUFLAGS ?= -M q35 -m 256M -serial stdio -display none -no-reboot -no-shutdown

.PHONY: all iso run debug test-exception clean distclean help

all: $(KERNEL)

iso: $(ISO)

run: $(ISO)
	$(QEMU) $(QEMUFLAGS) -cdrom $(ISO) -boot d

debug: $(ISO)
	@echo "QEMU is paused. In another terminal, run:"
	@echo "  gdb $(KERNEL) -ex 'target remote localhost:1234'"
	$(QEMU) $(QEMUFLAGS) -cdrom $(ISO) -boot d -S -s

test-exception:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)-exception CXXFLAGS="$(CXXFLAGS) -DOSCAR_TEST_EXCEPTION" run

help:
	@echo "make          Build the kernel ELF"
	@echo "make iso      Build a BIOS/UEFI bootable ISO"
	@echo "make run      Boot it in QEMU; serial output appears here"
	@echo "make debug    Boot paused and open QEMU's GDB stub"
	@echo "make clean    Remove build products"
	@echo "make distclean  Also remove downloaded dependencies"

$(BUILD_DIR):
	mkdir -p $@

$(DEPS_DIR):
	mkdir -p $@

$(PROTOCOL_DIR)/include/limine.h: | $(DEPS_DIR)
	rm -rf $(PROTOCOL_DIR)
	git clone --depth=1 https://github.com/Limine-Bootloader/limine-protocol.git $(PROTOCOL_DIR)

$(LIMINE_ARCHIVE): | $(DEPS_DIR)
	curl -fL https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz -o $@

$(LIMINE_DIR)/limine: $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	mkdir -p $(LIMINE_DIR)
	tar -xzf $(LIMINE_ARCHIVE) -C $(LIMINE_DIR) --strip-components=1
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"

$(BUILD_DIR)/%.o: src/%.cpp $(PROTOCOL_DIR)/include/limine.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@


$(BUILD_DIR)/asm/%.o: src/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) linker.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) -o $@

$(ISO): $(KERNEL) $(LIMINE_DIR)/limine limine.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp limine.conf $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $@
	$(LIMINE_DIR)/limine bios-install $@
	rm -rf $(ISO_ROOT)

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(DEPS_DIR)

-include $(patsubst %.o,%.d,$(filter %.o,$(OBJECTS)))
