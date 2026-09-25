# OScar x86-64 C++ kernel

A deliberately small freestanding C++ kernel. Limine loads the ELF kernel, and
the kernel initializes COM1, reads the Limine-provided memory map, initializes
a physical page allocator, manages kernel and process address-space structures and mappings,
validates user memory ranges, installs an IDT and PIT timer interrupts, provides
a kernel heap, creates and runs kernel-thread stacks, schedules kernel threads
cooperatively and from timer interrupts, supports tick-based waiting and waking,
provides interrupt-safe spinlocks, wait queues, and blocking mutexes,
prints diagnostics to QEMU's serial console, and idles.

This is a kernel seed, not yet an operating system. It has no user mode,
filesystem, or drivers beyond basic serial I/O.

## Repository layout

```text
.
├── Makefile          Builds the ELF, bootable ISO, and QEMU targets
├── linker.ld         Places the kernel in the x86-64 higher half
├── limine.conf       Limine boot entry
└── src/
    ├── arch/x86_64/  CPU contexts, IDT, and interrupt entry stubs
    ├── core/         Entry point, fatal error handling, threads, and scheduler
    ├── drivers/      Port I/O, serial output, and timer backends
    ├── interrupts/   Interrupt routing and CPU interrupt helpers
    ├── memory/       Physical pages, virtual memory, and kernel heap
    ├── synchronization/ Spinlocks, wait queues, and mutexes
    └── tests/        Boot-time subsystem smoke tests
```

Limine and its protocol header are downloaded into `deps/` on the first build.
Generated files go into `build/`.

## Recommended environment

Use a Linux system or Ubuntu under WSL2. On Ubuntu/Debian, install:

```sh
sudo apt update
sudo apt install build-essential clang lld make git curl xorriso qemu-system-x86 gdb
```

## Build and run

```sh
make iso
make run
make test-exception
```

QEMU runs headlessly and connects its emulated COM1 port to the terminal. A
normal run should show output similar to:

```text
Barebones kernel started.
Memory-map entries: 20
Physical pages: 64112 total, 64112 free
Spinlock smoke test passed.
Allocated physical pages: 0x0000000000073000, 0x0000000000074000
Physical page allocator smoke test passed.
Virtual memory mapping smoke test passed.
Process address-space smoke test passed.
Kernel heap smoke test passed.
Process structure smoke test passed.
Kernel thread stack and lifecycle smoke test passed.
Round-robin scheduler smoke test passed.
Timer preemption smoke test passed.
Waiting-thread sleep smoke test passed.
IDT initialized.
Interrupt controller: IOAPIC.
Timer interrupt smoke test passed.
Synchronization primitive smoke test passed.
Waiting-thread sleep smoke test passed.
Kernel initialization complete; halting.
```

The exact memory-map entry count and usable-page count may differ.

`make test-exception` builds a separate test kernel, executes `ud2`, and
prints the invalid-opcode exception and saved register state.

Press `Ctrl-A`, then `X`, to exit headless QEMU.

## Formatting and linting

The repository keeps its editor, lint, and formatting configuration in
`.clangd`, `.clang-tidy`, and `.clang-format`. From the repository root, run:

```sh
bear --output compile_commands.json -- make clean all
clang-format --dry-run --Werror $(find src -name '*.cpp' -o -name '*.hpp')
clang-tidy $(find src -name '*.cpp' -o -name '*.hpp') --config-file=.clang-tidy --warnings-as-errors="*"
```

The repository-local equivalent is:

```sh
make lint
```

It regenerates `compile_commands.json`, formats all C++ sources, and runs
clang-tidy with all diagnostics treated as errors.

If `clang-format` or `clang-tidy` is unavailable, use clangd's configured
check mode for every C++ source file:

```sh
find src -name '*.cpp' -print0 | xargs -0 -n1 clangd --check
```

Review source diagnostics from clangd; its editor code-action tests may report
internal failures that are not source lint errors.

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

## Notes about freestanding C++

The build disables exceptions, RTTI, stack protection, the red zone, and the
hosted C++ runtime. Consequently, ordinary standard-library facilities, global
constructors, `new`, exceptions, and OS services are not available until you
implement the necessary runtime support yourself.

## License

OScar is released under the MIT license. See [LICENSE](LICENSE) for the full
license text. Downloaded dependencies retain their own licenses.
