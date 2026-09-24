# OScar x86-64 C++ kernel

A deliberately small freestanding C++ kernel. Limine loads the ELF kernel, and
the kernel initializes COM1, reads the Limine-provided memory map, initializes
a physical page allocator, manages basic virtual-memory mappings, installs an
IDT, provides a kernel heap, prints diagnostics to QEMU's serial console, and
halts.

This is a kernel seed, not yet an operating system. It has no process address
spaces, scheduler, user mode, filesystem, or drivers beyond basic serial I/O.

## Repository layout

```text
.
├── Makefile          Builds the ELF, bootable ISO, and QEMU targets
├── linker.ld         Places the kernel in the x86-64 higher half
├── limine.conf       Limine boot entry
└── src/
    ├── main.cpp      Entry point and Limine requests
    ├── serial.*      COM1 serial output
    ├── panic.*       Fatal error handling
    ├── idt.*         Interrupt Descriptor Table setup
    ├── idt.S         x86-64 exception entry stubs
    ├── memory.*      Physical page allocator
    ├── virtual_memory.* Basic page-table mappings
    ├── kernel_heap.* Kernel dynamic allocation
    └── self_tests.*  Boot-time subsystem smoke tests
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
Physical pages: 64120 total, 64120 free
Allocated physical pages: 0x0000000000073000, 0x0000000000074000
Physical page allocator smoke test passed.
Virtual memory mapping smoke test passed.
Kernel heap smoke test passed.
IDT initialized.
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
clang-format --dry-run --Werror src/*.cpp src/*.hpp
clang-tidy src/*.cpp --config-file=.clang-tidy
```

If `clang-format` or `clang-tidy` is unavailable, use clangd's configured
check mode for every C++ source file:

```sh
for source in src/*.cpp; do clangd --check="$source"; done
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
