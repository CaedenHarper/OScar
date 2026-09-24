# OScar x86-64 C++ kernel

A deliberately small starting point for a freestanding C++ kernel. Limine loads
the ELF kernel, the kernel initializes COM1, prints to QEMU's serial console,
reads the Limine-provided memory map, and halts.

This is a kernel seed, not yet an operating system. It has no interrupts,
allocator, scheduler, user mode, filesystem, or drivers beyond basic serial I/O.

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
    └── idt.S         x86-64 exception entry stubs
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
```

QEMU runs headlessly and connects its emulated COM1 port to the terminal. You
should see output similar to:

```text
Barebones kernel started.
Memory-map entries: 12
Kernel initialization complete; halting.
```

The exact memory-map entry count may differ.

Press `Ctrl-A`, then `X`, to exit headless QEMU.

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
