# OScar x86-64 C++ kernel

A deliberately small freestanding C++ kernel. Limine loads the ELF kernel, and
the kernel initializes COM1, reads the Limine-provided memory map, initializes
a physical page allocator, manages kernel and process address-space structures and mappings,
validates user memory ranges, installs an IDT, GDT/TSS, and PIT timer
interrupts, creates ring-3 user threads with private kernel stacks, handles an
initial `int 0x80` system-call ABI (`write`, `exit`, `yield`, `sleep`, `getpid`,
`getid`, `spawn`, and `waitpid`, plus filesystem `open`, `create`, `read`, `write`,
`seek`, `close`, `mkdir`, `unlink`, and `rmdir`),
`chdir`, `getcwd`, `stat`, and `readdir`,
loads validated in-memory ELF64 executables with `PT_LOAD` segments, including
zero-filled memory and an initial user stack,
provides a kernel heap, creates and runs kernel-thread stacks, schedules kernel
threads cooperatively and from timer interrupts, supports tick-based waiting
and waking, provides interrupt-safe spinlocks, wait queues, and blocking
mutexes, exposes a block-device protocol with a polling legacy VirtIO block
driver, mounts an ext2 filesystem, resolves paths, and reads/writes regular
files through direct, single-indirect, and double-indirect data blocks,
including sparse holes, initializes an IRQ-driven PS/2 keyboard driver behind a
hardware-independent keyboard event queue, serial receive interrupts, and a serial-backed kernel terminal,
prints diagnostics to QEMU's serial console, and idles.

This is a kernel seed, not yet an operating system. C user programs can use
the public freestanding API in `user/include/oscar/syscalls.h`; its library
implementation hides the raw `int 0x80` ABI. It does not yet provide
`fork`, dynamic linking, or rename support. The filesystem now supports regular-file
creation, `mkdir`, `unlink`, empty-directory removal, working-directory navigation,
metadata lookup, and directory enumeration. `spawn` loads a validated ELF from the mounted filesystem, and `waitpid` waits for
that exact child and returns its exit status. After kernel startup, the kernel
loads `/sbin/init` from the filesystem; `init` then starts `/bin/cash` and
waits for it in user space. `cash` is the initial user-space interactive shell
with `cd`, `pwd`, and `exit` built-ins; its first external command is the
filesystem-backed `/bin/ls` program.

## Repository layout

```text
.
├── Makefile          Builds the ELF, bootable ISO, and QEMU targets
├── linker.ld         Places the kernel in the x86-64 higher half
├── limine.conf       Limine boot entry
├── user/              Public C syscall headers and freestanding wrappers
└── src/
    ├── arch/x86_64/  CPU contexts, GDT/TSS, IDT, and entry stubs
    ├── core/         Entry point, processes, threads, scheduler, and syscalls
    ├── drivers/      Port I/O, serial, timer, keyboard input, terminal, PS/2, and VirtIO block support
    ├── exec/         ELF64 validation and executable loading
    ├── filesystem/   VFS, ext2 mounting, path lookup, regular-file I/O, and mutation
    ├── interrupts/   Interrupt routing and CPU interrupt helpers
    ├── memory/       Physical pages, virtual memory, and kernel heap
    ├── synchronization/ Spinlocks, wait queues, and mutexes
    ├── storage/      Hardware-independent block-device protocol
    ├── tests/        Boot-time subsystem smoke tests
    └── tests/filesystem/ Files copied into the generated ext2 test image
```

Limine and its protocol header are downloaded into `deps/` on the first build.
The build compiles the user ELF fixtures in `tests/user/`—including the basic,
filesystem-read, computed-prime, second-program, interactive `cash` shell, and
filesystem-backed `init` success cases plus intentional-crash programs—and
embeds the test-only images into the kernel smoke tests. The kernel also tests
malformed ELF metadata directly before scheduling user processes.
Generated files go into `build/`.

`make run` creates and attaches the persistent `build/virtio-test.img` disk to
QEMU. The image is ignored by Git and remains across emulator runs; `make
clean` removes it with the other build products.
The image is formatted as ext2 and populated with filesystem lookup and
writable-file fixtures, including a generated 300 KiB file for indirect-block
reads and a directory with 300 entries for indirect-directory lookup tests.
The boot-time filesystem suite writes and reads back a deterministic 300 KiB
pattern across direct, single-indirect, and double-indirect blocks, checks
zero-filled extension gaps, and reopens the file to verify persistence;
install `mke2fs` and `debugfs` in addition to the tools listed below.

## Recommended environment

Use a Linux system or Ubuntu under WSL2. On Ubuntu/Debian, install:

```sh
sudo apt update
sudo apt install build-essential clang lld make git curl xorriso e2fsprogs qemu-system-x86 gdb
```

## Build and run

```sh
make iso
make run
```

`make test-exception` builds a separate test kernel, executes `ud2`, and
prints the invalid-opcode exception and saved register state.

Press `Ctrl-A`, then `X`, to exit headless QEMU.

## Formatting and linting

The repository keeps its editor, lint, and formatting configuration in
`.clangd`, `.clang-tidy`, and `.clang-format`. From the repository root, run:

```sh
bear --output compile_commands.json -- make clean all
clang-format --dry-run --Werror $(find src -name '*.cpp' -o -name '*.hpp') $(find tests/user -name '*.c')
clang-tidy $(find src -name '*.cpp' -o -name '*.hpp') $(find tests/user -name '*.c') --config-file=.clang-tidy --warnings-as-errors="*"
```

The repository-local equivalent is:

```sh
make lint
```

## Debug with GDB

Run QEMU paused:

```sh
make debug
```

Then, in a second terminal:

```sh
gdb build/kernel.elf
(gdb) target remote localhost:1234
(gdb) break kmain
(gdb) continue
```

Useful commands include `info registers`, `x/10i $rip`, `stepi`, and `bt`.

## License

OScar is released under the MIT license. See [LICENSE](LICENSE) for the full
license text. Downloaded dependencies retain their own licenses.
