# OScar x86-64 C++ kernel

A deliberately small freestanding C++ kernel loaded by Limine and tested in
QEMU. It is an educational kernel seed rather than a complete operating
system.

## Current capabilities

The kernel currently provides:

- Physical and virtual memory management, a kernel heap, and user address spaces.
- GDT/TSS, IDT, timer interrupts, interrupt dispatch, kernel threads, and a
  round-robin scheduler.
- Ring-3 user processes with validated ELF64 loading, private kernel stacks,
  initial user stacks, `argc`/`argv`, and crash isolation.
- An `int 0x80` system-call ABI covering process control, descriptors, pipes,
  filesystem operations, and IPv4/TCP sockets.
- Synchronization primitives including spinlocks, wait queues, and mutexes.
- PCI enumeration, polling VirtIO block and network drivers, DHCP network
  configuration, and an IRQ-driven PS/2 keyboard backend.
- An ext2 filesystem with VFS path lookup, regular-file I/O, directories,
  permissions, ownership, metadata, corruption checks, and mutation support.
- Ethernet, ARP, IPv4, ICMP, UDP, TCP, DNS, and an HTTP client with incremental
  response parsing and streaming body delivery.
- A serial-backed terminal and the user-space `cash` shell.

## User-space environment

After startup, the kernel loads `/sbin/init` from the filesystem. `init` starts
the interactive `/bin/cash` shell and waits for it in user space.

The shell includes these built-ins:

- `cd`, `pwd`, `echo`, `help`, and `exit`.

Other commands are ordinary filesystem-backed user programs, including
`mkdir`, `touch`, `cp`, `mv`, `rm`, `cat`, `ls`, `ps`, `top`, `df`, `du`,
`mount`, `kill`, `ping`, `nslookup`, and `httpget`.

C user programs can use the public freestanding API in
`user/include/oscar/syscalls.h`; its library implementation hides the raw
`int 0x80` ABI. C programs use a conventional `main()` entry point;
`user/lib/crt0.c` supplies the ELF `_start` shim and converts `main`'s return
value into `oscar_exit`. `spawn` loads a validated ELF from the mounted filesystem,
inherits the parent's descriptor table, and constructs the child's initial
arguments. `waitpid` waits for that exact child and returns its exit status.

The initial libc also provides POSIX-shaped headers and wrappers for string and
memory operations, integer conversion, `errno`, file descriptors, paths,
metadata, pipes, descriptor duplication, process IDs, and basic sleeping.
Memory allocation, buffered `stdio`, directory streams, signals, and
`fork`/`exec` are not implemented yet.

## Current limitations

OScar does not yet provide `fork`, dynamic linking, rename support, HTTPS,
redirects, DHCP, `/etc/resolv.conf`, or full mount lifecycle management.
Signal delivery and a more complete C library remain future milestones.

## Repository layout

```text
.
├── Makefile          Builds the ELF, bootable ISO, and QEMU targets
├── linker.ld         Places the kernel in the x86-64 higher half
├── limine.conf       Limine boot entry
├── user/              Public C headers and freestanding user-space libraries
└── src/
    ├── arch/x86_64/  CPU contexts, GDT/TSS, IDT, and entry stubs
    ├── core/         Entry point, processes, threads, scheduler, and syscalls
    ├── drivers/      Port I/O, PCI discovery, serial, timer, keyboard input, terminal, PS/2, and VirtIO block/network support
    ├── network/      Ethernet framing, ARP/IPv4, ICMP, UDP, TCP connections, and DNS support
    ├── exec/         ELF64 validation and executable loading
    ├── filesystem/   VFS, ext2 mounting, path lookup, regular-file I/O, and mutation
    ├── interrupts/   IRQ registration, PIC/IOAPIC routing, and CPU interrupt helpers
    ├── memory/       Physical pages, virtual memory, and kernel heap
    ├── synchronization/ Spinlocks, wait queues, and mutexes
    ├── storage/      Hardware-independent block-device protocol
    ├── tests/        Boot-time subsystem smoke tests and user programs
    └── tests/filesystem/ Files copied into generated ext2 images
```

Limine and its protocol header are downloaded into `deps/` on the first build.
The normal build compiles the runtime user programs in `tests/user/`, including
filesystem-backed `init`, `cash`, and the shell utilities. These programs link the
reusable freestanding libraries in `user/lib/`. Test fixtures and the
boot-time smoke-test sources are excluded from the normal kernel. Generated
files go into `build/`.

`make run` creates and attaches the persistent `build/virtio.img` disk and a
QEMU user-mode VirtIO network device to QEMU.
The image is ignored by Git and remains across emulator runs; `make clean`
removes it with the other build products. It contains the runtime programs and
basic filesystem fixtures.

`make test` creates a separate `build-test/` kernel and
`build-test/virtio-test.img`, copies the image to a per-run temporary disk,
runs the complete kernel and user-space smoke suite in headless QEMU, and
succeeds only after it sees `OSCAR TESTS PASSED`. This prevents an interrupted
QEMU process from locking or modifying the base test image.
The test image includes the large indirect-block fixture, 300-entry directory,
test executables, and test-only `/sbin/init`. The test suite covers filesystem
PCI enumeration and VirtIO BAR discovery,
read/write/mutation, permission enforcement, ownership metadata, corruption
rejection, descriptor duplication and pipes, malformed and crashing ELFs, scheduling,
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
