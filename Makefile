SHELL := /bin/sh
.DELETE_ON_ERROR:

BUILD_DIR := build
USER_BUILD_DIR := $(BUILD_DIR)/user
RUNTIME_USER_PROGRAM_NAMES := init cash ls top
TEST_USER_PROGRAM_NAMES := basic prime second filesystem divzero kernel_access invalid_opcode init test_init cash ls top shell_commands argv_test kill_target
ifeq ($(OSCAR_TEST_SUITE),1)
USER_PROGRAM_NAMES := $(TEST_USER_PROGRAM_NAMES)
VIRTIO_DISK := $(BUILD_DIR)/virtio-test.img
else
USER_PROGRAM_NAMES := $(RUNTIME_USER_PROGRAM_NAMES)
VIRTIO_DISK := $(BUILD_DIR)/virtio.img
endif
USER_ELFS := $(addprefix $(USER_BUILD_DIR)/,$(addsuffix .elf,$(USER_PROGRAM_NAMES)))
DEPS_DIR := deps
ISO_ROOT := $(BUILD_DIR)/iso_root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/barebones-kernel.iso
LARGE_FILESYSTEM_TEST_FILE := $(BUILD_DIR)/filesystem-large.bin
FILESYSTEM_TEST_FILES := tests/filesystem/hello.txt tests/filesystem/config.txt tests/filesystem/writable.txt
CPP_SOURCES := $(filter-out src/tests/%,$(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp))
ASM_SOURCES := $(filter-out src/tests/%,$(wildcard src/*.S src/*/*.S src/*/*/*.S))
ifeq ($(OSCAR_TEST_SUITE),1)
CPP_SOURCES += $(wildcard src/tests/*.cpp)
ASM_SOURCES += $(wildcard src/tests/*.S)
endif
LINT_CPP_FILES := $(shell find src -name '*.cpp' -o -name '*.hpp')
LINT_USER_C_FILES := $(shell find tests/user -name '*.c') $(shell find user -name '*.c')
LINT_TEST_C_FILES := $(shell find tests/user -name '*.c')
LINT_USER_LIB_C_FILES := $(shell find user -name '*.c')
LINT_USER_HEADERS := $(shell find user -name '*.h')
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
ifeq ($(OSCAR_TEST_SUITE),1)
CXXFLAGS += -DOSCAR_TEST_SUITE
endif
ASFLAGS := \
	-target x86_64-unknown-none-elf \
	-ffreestanding -fno-pie -mno-red-zone -Wa,-I$(BUILD_DIR)
LDFLAGS := \
	-target x86_64-unknown-none-elf -fuse-ld=lld \
	-nostdlib -static \
	-Wl,-T,linker.ld \
	-Wl,--gc-sections \
	-Wl,--build-id=none \
	-Wl,-z,max-page-size=0x1000 \
	-Wl,-z,noexecstack

QEMUFLAGS ?= -M q35 -m 256M -serial stdio -display none -no-reboot -no-shutdown

.PHONY: all iso run debug test test-run test-exception lint clean distclean help

all: $(KERNEL)

iso: $(ISO)

run: $(ISO) $(VIRTIO_DISK)
	$(QEMU) $(QEMUFLAGS) -drive file=$(VIRTIO_DISK),format=raw,if=none,id=virtio-disk -device virtio-blk-pci,disable-modern=on,drive=virtio-disk -cdrom $(ISO) -boot d

debug: $(ISO) $(VIRTIO_DISK)
	@echo "QEMU is paused. In another terminal, run:"
	@echo "  gdb $(KERNEL) -ex 'target remote localhost:1234'"
	$(QEMU) $(QEMUFLAGS) -drive file=$(VIRTIO_DISK),format=raw,if=none,id=virtio-disk -device virtio-blk-pci,disable-modern=on,drive=virtio-disk -cdrom $(ISO) -boot d -S -s

test-exception:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)-exception OSCAR_TEST_SUITE=1 CXXFLAGS="$(CXXFLAGS) -DOSCAR_TEST_SUITE -DOSCAR_TEST_EXCEPTION" run

test:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)-test OSCAR_TEST_SUITE=1 test-run

test-run: $(ISO) $(VIRTIO_DISK)
	@rm -f $(BUILD_DIR)/test-output.log
	@qemu_pid=0; completed=0; elapsed=0; \
	$(QEMU) $(filter-out -serial stdio,$(QEMUFLAGS)) -serial stdio -monitor none -drive file=$(VIRTIO_DISK),format=raw,if=none,id=virtio-disk -device virtio-blk-pci,disable-modern=on,drive=virtio-disk -cdrom $(ISO) -boot d >$(BUILD_DIR)/test-output.log 2>/dev/null & qemu_pid=$$!; \
	while [ $$elapsed -lt 150 ]; do \
		if grep -q "OSCAR TESTS PASSED" $(BUILD_DIR)/test-output.log; then completed=1; kill $$qemu_pid 2>/dev/null || true; break; fi; \
		if ! kill -0 $$qemu_pid 2>/dev/null; then break; fi; \
		sleep 0.1; elapsed=$$((elapsed + 1)); \
	done; \
	wait $$qemu_pid 2>/dev/null || true; \
	cat $(BUILD_DIR)/test-output.log; \
	if [ $$completed -ne 1 ]; then echo "Test suite did not complete within 15 seconds." >&2; exit 1; fi
	@grep -q "OSCAR TESTS PASSED" $(BUILD_DIR)/test-output.log
	@echo "All kernel and user-space tests passed."

lint:
	bear --output compile_commands.json -- $(MAKE) clean all OSCAR_TEST_SUITE=1
	clang-format --dry-run --Werror $(LINT_CPP_FILES) $(LINT_USER_C_FILES) $(LINT_USER_HEADERS)
	clang-tidy $(LINT_CPP_FILES) --config-file=.clang-tidy --warnings-as-errors="*"
	clang-tidy --extra-arg-before=-x --extra-arg-before=c $(LINT_TEST_C_FILES) --config-file=.clang-tidy --warnings-as-errors="*"
	clang-tidy $(LINT_USER_LIB_C_FILES) --config-file=.clang-tidy --warnings-as-errors="*"

help:
	@echo "make          Build the kernel ELF"
	@echo "make iso      Build a BIOS/UEFI bootable ISO"
	@echo "make run      Boot it in QEMU; serial output appears here"
	@echo "make test     Build a separate test kernel/image and run all smoke tests"
	@echo "make debug    Boot paused and open QEMU's GDB stub"
	@echo "make lint     Build a compile database and run format/lint checks"
	@echo "make clean    Remove build products"
	@echo "make distclean  Also remove downloaded dependencies"

$(BUILD_DIR):
	mkdir -p $@

ifeq ($(OSCAR_TEST_SUITE),1)
IMAGE_PROGRAMS := $(USER_BUILD_DIR)/second.elf $(USER_BUILD_DIR)/init.elf $(USER_BUILD_DIR)/test_init.elf $(USER_BUILD_DIR)/cash.elf $(USER_BUILD_DIR)/ls.elf $(USER_BUILD_DIR)/top.elf $(USER_BUILD_DIR)/shell_commands.elf $(USER_BUILD_DIR)/argv_test.elf $(USER_BUILD_DIR)/kill_target.elf
else
IMAGE_PROGRAMS := $(USER_BUILD_DIR)/init.elf $(USER_BUILD_DIR)/cash.elf $(USER_BUILD_DIR)/ls.elf $(USER_BUILD_DIR)/top.elf
endif

$(VIRTIO_DISK): Makefile $(FILESYSTEM_TEST_FILES) $(IMAGE_PROGRAMS) $(if $(filter 1,$(OSCAR_TEST_SUITE)),$(LARGE_FILESYSTEM_TEST_FILE),) | $(BUILD_DIR)
	truncate -s 8M $@
	mke2fs -q -F -t ext2 -b 1024 $@
	debugfs -w -R 'mkdir /etc' $@
	debugfs -w -R 'mkdir /etc/oscar' $@
	debugfs -w -R 'mkdir /many' $@
	debugfs -w -R 'mkdir /bin' $@
	debugfs -w -R 'mkdir /sbin' $@
	debugfs -w -R 'write tests/filesystem/hello.txt /hello.txt' $@
	debugfs -w -R 'write tests/filesystem/config.txt /etc/oscar/config.txt' $@
	debugfs -w -R 'write tests/filesystem/writable.txt /writable.txt' $@
	debugfs -w -R 'write $(USER_BUILD_DIR)/cash.elf /bin/cash' $@
	debugfs -w -R 'write $(USER_BUILD_DIR)/ls.elf /bin/ls' $@
	debugfs -w -R 'write $(USER_BUILD_DIR)/top.elf /bin/top' $@
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),true,debugfs -w -R 'write $(USER_BUILD_DIR)/init.elf /sbin/init' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),debugfs -w -R 'write $(USER_BUILD_DIR)/second.elf /bin/second.elf' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),debugfs -w -R 'write $(USER_BUILD_DIR)/shell_commands.elf /bin/shell_commands' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),debugfs -w -R 'write $(USER_BUILD_DIR)/argv_test.elf /bin/argv_test' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),debugfs -w -R 'write $(USER_BUILD_DIR)/kill_target.elf /bin/kill_target' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),debugfs -w -R 'write $(USER_BUILD_DIR)/test_init.elf /sbin/init' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),debugfs -w -R 'write $(LARGE_FILESYSTEM_TEST_FILE) /large.bin' $@)
	$(if $(filter 1,$(OSCAR_TEST_SUITE)),index=0; while [ $$index -lt 300 ]; do \
		debugfs -w -R "write tests/filesystem/hello.txt /many/file$$index" $@ >/dev/null || exit 1; \
		index=$$((index + 1)); \
	done)

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
