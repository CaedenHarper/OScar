# OScar x86-64 C++ kernel

A deliberately small freestanding C++ kernel. Limine loads the ELF kernel, and
the kernel initializes COM1, reads the Limine-provided memory map, initializes
a physical page allocator, manages kernel and process address-space structures and mappings,
validates user memory ranges, installs an IDT, GDT/TSS, and PIT timer
interrupts, creates ring-3 user threads with private kernel stacks, handles an
initial `int 0x80` system-call ABI (`write`, `exit`, `yield`, `sleep`, `getpid`,
`getid`, `spawn`, `waitpid`, `dup`, `dup2`, and `pipe`, plus filesystem `open`, `create`, `read`, `write`,
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
including serial ANSI navigation sequences and basic cursor-aware line editing,
prints diagnostics to QEMU's serial console, and idles.

This is a kernel seed, not yet an operating system. C user programs can use
the public freestanding API in `user/include/oscar/syscalls.h`; its library
implementation hides the raw `int 0x80` ABI. It does not yet provide
`fork`, dynamic linking, or rename support. `spawn` now constructs an initial
`argc`/`argv` user stack and inherits the parent's descriptor table. The
filesystem now supports regular-file
creation, `mkdir`, `unlink`, empty-directory removal, working-directory navigation,
metadata lookup, and directory enumeration. `spawn` loads a validated ELF from the mounted filesystem, and `waitpid` waits for
that exact child and returns its exit status. After kernel startup, the kernel
loads `/sbin/init` from the filesystem; `init` then starts `/bin/cash` and
waits for it in user space. `cash` is the initial user-space interactive shell
with `cd`, `pwd`, `echo`, `help`, and `exit` built-ins. Its current filesystem
and process commands are implemented in the shell using the public syscall
API: `mkdir`, `touch`, `cp`, `mv`, `rm`, `cat`, `ps`, and `kill`. The shell also dispatches filesystem-backed `/bin/ls` and
bounded-refresh `/bin/top`. The shell command smoke program exercises the
filesystem operations and process inspection during init startup; general
external command dispatch now passes argument vectors to child programs, while
signal delivery remains a later milestone.

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
    ├── tests/        Boot-time subsystem smoke tests and user programs
    └── tests/filesystem/ Files copied into generated ext2 images
```

Limine and its protocol header are downloaded into `deps/` on the first build.
The normal build compiles only the runtime user programs in `tests/user/`:
filesystem-backed `init`, `cash`, `ls`, and `top`. Test fixtures and the
boot-time smoke-test sources are excluded from the normal kernel. Generated
files go into `build/`.

`make run` creates and attaches the persistent `build/virtio.img` disk to QEMU.
The image is ignored by Git and remains across emulator runs; `make clean`
removes it with the other build products. It contains the runtime programs and
basic filesystem fixtures.

`make test` creates a separate `build-test/` kernel and
`build-test/virtio-test.img`, runs the complete kernel and user-space smoke
suite in headless QEMU, and succeeds only after it sees `OSCAR TESTS PASSED`.
The test image includes the large indirect-block fixture, 300-entry directory,
test executables, and test-only `/sbin/init`. The test suite covers filesystem
read/write/mutation behavior, descriptor duplication and pipes, malformed and crashing ELFs, scheduling,
interrupts, synchronization, process creation, descriptor inheritance, and
shell commands. Install `mke2fs` and `debugfs` in addition to the tools below.

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
make test
```

`make test-exception` builds a separate test kernel, executes `ud2`, and
prints the invalid-opcode exception and saved register state.

Press `Ctrl-A`, then `X`, to exit headless QEMU.

## Formatting and linting

The repository keeps its editor, lint, and formatting configuration in
`.clangd`, `.clang-tidy`, and `.clang-format`. From the repository root, run:

```sh
bear --output compile_commands.json -- make BUILD_DIR=build-lint clean all OSCAR_TEST_SUITE=1
clang-format --dry-run --Werror $(find src tests user -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \))
clang-tidy $(find src tests user -type f \( -name '*.cpp' -o -name '*.hpp' \)) --config-file=.clang-tidy --warnings-as-errors="*"
clang-tidy --extra-arg-before=-x --extra-arg-before=c $(find src tests user -type f \( -name '*.c' -o -name '*.h' \)) --config-file=.clang-tidy --warnings-as-errors="*"
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
