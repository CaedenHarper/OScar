SHELL := /bin/sh
.DELETE_ON_ERROR:

BUILD_DIR := build
USER_BUILD_DIR := $(BUILD_DIR)/user
USER_PROGRAM_NAMES := basic prime second filesystem divzero kernel_access invalid_opcode init terminal
USER_ELFS := $(addprefix $(USER_BUILD_DIR)/,$(addsuffix .elf,$(USER_PROGRAM_NAMES)))
DEPS_DIR := deps
ISO_ROOT := $(BUILD_DIR)/iso_root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/barebones-kernel.iso
VIRTIO_DISK := $(BUILD_DIR)/virtio-test.img
LARGE_FILESYSTEM_TEST_FILE := $(BUILD_DIR)/filesystem-large.bin
FILESYSTEM_TEST_FILES := tests/filesystem/hello.txt tests/filesystem/config.txt
CPP_SOURCES := $(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp)
ASM_SOURCES := $(wildcard src/*.S src/*/*.S src/*/*/*.S)
LINT_CPP_FILES := $(shell find src -name '*.cpp' -o -name '*.hpp')
LINT_USER_C_FILES := $(shell find tests/user -name '*.c') $(shell find user -name '*.c' -o -name '*.h')
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES)) \
	$(patsubst src/%.S,$(BUILD_DIR)/asm/%.o,$(ASM_SOURCES))

CXX := clang++
HOST_CC ?= cc
QEMU ?= qemu-system-x86_64

LIMINE_DIR := $(DEPS_DIR)/limine
PROTOCOL_DIR := $(DEPS_DIR)/limine-protocol
LIMINE_ARCHIVE := $(DEPS_DIR)/limine-binary.tar.gz

CPPFLAGS := -I$(PROTOCOL_DIR)/include \
	-Isrc/arch/x86_64 -Isrc/core -Isrc/drivers -Isrc/filesystem -Isrc/interrupts -Isrc/memory \
	-Isrc/synchronization -Isrc/tests -Isrc/exec -Isrc/storage
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

.PHONY: all iso run debug test-exception lint clean distclean help

all: $(KERNEL)

iso: $(ISO)

run: $(ISO) $(VIRTIO_DISK)
	$(QEMU) $(QEMUFLAGS) -drive file=$(VIRTIO_DISK),format=raw,if=none,id=virtio-disk -device virtio-blk-pci,disable-modern=on,drive=virtio-disk -cdrom $(ISO) -boot d

debug: $(ISO) $(VIRTIO_DISK)
	@echo "QEMU is paused. In another terminal, run:"
	@echo "  gdb $(KERNEL) -ex 'target remote localhost:1234'"
	$(QEMU) $(QEMUFLAGS) -drive file=$(VIRTIO_DISK),format=raw,if=none,id=virtio-disk -device virtio-blk-pci,disable-modern=on,drive=virtio-disk -cdrom $(ISO) -boot d -S -s

test-exception:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)-exception CXXFLAGS="$(CXXFLAGS) -DOSCAR_TEST_EXCEPTION" run

lint:
	bear --output compile_commands.json -- $(MAKE) clean all
	clang-format --dry-run --Werror $(LINT_CPP_FILES) $(LINT_USER_C_FILES)
	clang-tidy $(LINT_CPP_FILES) $(LINT_USER_C_FILES) --config-file=.clang-tidy --warnings-as-errors="*"

help:
	@echo "make          Build the kernel ELF"
	@echo "make iso      Build a BIOS/UEFI bootable ISO"
	@echo "make run      Boot it in QEMU; serial output appears here"
	@echo "make debug    Boot paused and open QEMU's GDB stub"
	@echo "make lint     Build a compile database and run format/lint checks"
	@echo "make clean    Remove build products"
	@echo "make distclean  Also remove downloaded dependencies"

$(BUILD_DIR):
	mkdir -p $@

$(VIRTIO_DISK): Makefile $(FILESYSTEM_TEST_FILES) $(LARGE_FILESYSTEM_TEST_FILE) $(USER_BUILD_DIR)/second.elf | $(BUILD_DIR)
	truncate -s 8M $@
	mke2fs -q -F -t ext2 -b 1024 $@
	debugfs -w -R 'mkdir /etc' $@
	debugfs -w -R 'mkdir /etc/oscar' $@
	debugfs -w -R 'mkdir /many' $@
	debugfs -w -R 'mkdir /bin' $@
	debugfs -w -R 'write tests/filesystem/hello.txt /hello.txt' $@
	debugfs -w -R 'write tests/filesystem/config.txt /etc/oscar/config.txt' $@
	debugfs -w -R 'write $(USER_BUILD_DIR)/second.elf /bin/second.elf' $@
	debugfs -w -R 'write $(LARGE_FILESYSTEM_TEST_FILE) /large.bin' $@
	index=0; while [ $$index -lt 300 ]; do \
		debugfs -w -R "write tests/filesystem/hello.txt /many/file$$index" $@ >/dev/null || exit 1; \
		index=$$((index + 1)); \
	done

$(LARGE_FILESYSTEM_TEST_FILE): | $(BUILD_DIR)
	dd if=/dev/zero of=$@ bs=1024 count=300 status=none

$(USER_BUILD_DIR):
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
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@


$(BUILD_DIR)/asm/%.o: src/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(ASFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/%.elf: tests/user/%.S tests/user/linker.ld | $(USER_BUILD_DIR)
	$(CXX) -target x86_64-unknown-none-elf -ffreestanding -fno-pie -mno-red-zone -c $< -o $(USER_BUILD_DIR)/$*.o
	$(CXX) -target x86_64-unknown-none-elf -fuse-ld=lld -nostdlib -static -Wl,-T,tests/user/linker.ld -Wl,--build-id=none $(USER_BUILD_DIR)/$*.o -o $@

$(USER_BUILD_DIR)/%.elf: tests/user/%.c user/include/oscar/syscalls.h user/lib/syscalls.c tests/user/linker.ld | $(USER_BUILD_DIR)
	$(CXX) -x c -target x86_64-unknown-none-elf -Iuser/include -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -c tests/user/$*.c -o $(USER_BUILD_DIR)/$*.o
	$(CXX) -x c -target x86_64-unknown-none-elf -Iuser/include -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -c user/lib/syscalls.c -o $(USER_BUILD_DIR)/$*.syscalls.o
	$(CXX) -target x86_64-unknown-none-elf -fuse-ld=lld -nostdlib -static -Wl,-T,tests/user/linker.ld -Wl,--build-id=none $(USER_BUILD_DIR)/$*.o $(USER_BUILD_DIR)/$*.syscalls.o -o $@

$(BUILD_DIR)/asm/tests/user_program.o: $(USER_BUILD_DIR)/basic.elf
$(BUILD_DIR)/asm/tests/user_program_prime.o: $(USER_BUILD_DIR)/prime.elf
$(BUILD_DIR)/asm/tests/user_program_second.o: $(USER_BUILD_DIR)/second.elf
$(BUILD_DIR)/asm/tests/user_program_filesystem.o: $(USER_BUILD_DIR)/filesystem.elf
$(BUILD_DIR)/asm/tests/user_program_divzero.o: $(USER_BUILD_DIR)/divzero.elf
$(BUILD_DIR)/asm/tests/user_program_kernel_access.o: $(USER_BUILD_DIR)/kernel_access.elf
$(BUILD_DIR)/asm/tests/user_program_invalid_opcode.o: $(USER_BUILD_DIR)/invalid_opcode.elf
$(BUILD_DIR)/asm/tests/user_program_init.o: $(USER_BUILD_DIR)/init.elf
$(BUILD_DIR)/asm/tests/user_program_terminal.o: $(USER_BUILD_DIR)/terminal.elf

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
