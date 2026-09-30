# OScar contribution guide

## Project overview

OScar is a small educational x86-64 operating-system kernel written in
freestanding C++ with a small amount of assembly. Limine loads the kernel ELF,
and QEMU provides the primary development and test environment. The kernel
initializes COM1 serial output, reads the Limine memory map, manages physical
pages and basic virtual-memory mappings, provides a kernel heap, installs the
GDT/TSS, Interrupt Descriptor Table, and hardware timer interrupts, creates
  ring-3 user threads, handles an initial system-call ABI including writable
  regular-file and basic directory operations, working directories, metadata and
  enumeration, descriptor duplication, in-memory pipes, and `spawn`/`waitpid`, loads validated
in-memory ELF64 executables, provides kernel synchronization primitives and
process structures, runs boot-time smoke tests, reports exception register
state, and halts.

This is a bare-metal kernel rather than a hosted application. The normal C++
runtime, standard library, exceptions, RTTI, and operating-system services are
not available unless the project explicitly implements the required support.
Keep new code suitable for the freestanding build and avoid introducing
hosted-runtime assumptions.

## Repository layout

- `src/`: kernel C++ modules and architecture-specific assembly, organized by
  architecture, core, drivers, interrupts, memory, and tests.
- `linker.ld`: higher-half kernel linker script and ELF entry point.
- `limine.conf`: Limine boot configuration.
- `Makefile`: kernel, ISO, QEMU, and exception-test targets.
- `deps/`: downloaded Limine dependencies; generated and ignored.
- `build/`: compiled objects, kernel ELF, and ISO output; generated and ignored.

The source tree is organized as follows:

- `src/arch/x86_64/`: CPU contexts, GDT/TSS, IDT setup, and exception/IRQ/syscall entry.
- `src/core/`: the kernel entry point, fatal error handling, processes, kernel
  and user threads, scheduler, and system calls.
- `src/drivers/`: x86 port I/O, serial output, timer/PIT support, the generic
  keyboard event layer, the serial-backed terminal, the IRQ-driven PS/2 backend,
  the COM1 receive-interrupt backend, and the polling legacy VirtIO block driver.
- `src/filesystem/`: the generic VFS, ext2 mounting, inode loading, path
  lookup, regular-file reads/writes through direct and indirect data blocks,
  and basic file/directory mutation.
- `src/exec/`: freestanding ELF64 validation and process image loading,
  including filesystem-backed child creation.
- `src/interrupts/`: interrupt-controller routing and CPU interrupt helpers.
- `src/memory/`: physical pages, virtual-memory mappings, user-memory copying,
  and kernel heap.
- `src/synchronization/`: spinlocks, wait queues, and blocking mutexes.
- `src/storage/`: hardware-independent block-device protocol.
- `src/tests/`: boot-time subsystem smoke tests; keep them out of `main.cpp`.
  `self_tests.cpp` coordinates the suite, while `self_tests_*.cpp` group
  context, keyboard, memory/process, scheduler, synchronization, and user-mode tests.
  `self_tests_internal.hpp` contains private cross-test declarations.
- `tests/user/`: source and linker script for successful, filesystem-read,
  computed-output, interactive `cash` shell, filesystem-backed init, shell
  command, and intentional-crash user ELF smoke images. Init, `cash`, and the
  shell-command smoke image are mounted from the generated ext2 image; the
  other test images are embedded in the kernel smoke tests. Malformed ELF metadata tests live in
  `src/tests/` because they exercise validation without loading an image.
- `user/include/oscar/`: public C headers for the user-space syscall API.
- `user/lib/`: freestanding C implementations of the user-space syscall wrappers.
- `tests/filesystem/`: source files copied into the generated ext2 test image.

## Build and test

Use the following checks before submitting changes:

```sh
make all
make run
make test
make test-exception
```

`make run` should boot the production kernel in headless QEMU and print serial
output. It attaches the persistent generated `build/virtio.img` runtime disk;
`make clean` removes that image. The production kernel excludes `src/tests/`
and boots the small filesystem-backed `init` without the exhaustive smoke
suite.
`make test` builds an isolated `build-test/` kernel and
`build-test/virtio-test.img` with all kernel and user-space tests, runs QEMU
for a bounded interval, and verifies the `OSCAR TESTS PASSED` marker. Test
artifacts are disposable and are removed by `make BUILD_DIR=build-test clean`.
The image is formatted as ext2 and populated with lookup fixtures during its
first build. The generated `large.bin` fixture crosses the single- and
double-indirect data-block boundaries, and `/many` contains 300 entries for
indirect-directory lookup tests. Filesystem changes should keep those smoke
tests passing. The writable-filesystem test also writes a deterministic 300 KiB
pattern across direct, single-indirect, and double-indirect blocks, verifies
zero-filled extension gaps and persisted metadata, and checks that read-only
handles reject writes. `mke2fs` and `debugfs` are required.
The mutation test additionally creates and removes regular files and nested
directories, rejects duplicate names and non-empty directory removal, and
cleans up its entries for repeatable persistent-image runs.
The user-space `cash` fixture exercises terminal line input, its `cd`, `pwd`,
`echo`, `help`, and `exit` built-ins, filesystem commands, external `/bin/ls`
dispatch, and the bounded-refresh `/bin/top` process monitor. The
`shell_commands` fixture validates the filesystem-command results and process
inspection during init startup. The `argv_test` fixture validates initial
`argc`/`argv` construction, descriptor inheritance, and pipe/descriptor-duplication
semantics. External command
dispatch now passes argument vectors; signal delivery remains a separate
shell milestone.
`make test-exception` builds in a separate `build-exception/` directory,
executes `ud2`, and should print an invalid-opcode diagnostic with register
state before halting.

For a clean build, use `make clean`. Do not commit generated files from
`build/` or downloaded dependencies from `deps/`.

## Formatting and linting

The repository's tool configuration is kept in these files:

- `.clangd`: clangd editor integration and clang-tidy diagnostic behavior.
- `.clang-tidy`: enabled clang-tidy checks and warning policy.
- `.clang-format`: C++ formatting rules, including four-space indentation,
  brace style, include ordering, and a 120-column limit.

Run the available C++ formatting and lint checks from the repository root:

```sh
bear --output compile_commands.json -- make BUILD_DIR=build-lint clean all OSCAR_TEST_SUITE=1
clang-format --dry-run --Werror $(find src tests user -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \))
clang-tidy $(find src tests user -type f \( -name '*.cpp' -o -name '*.hpp' \)) --config-file=.clang-tidy --warnings-as-errors="*"
clang-tidy --extra-arg-before=-x --extra-arg-before=c $(find src tests user -type f \( -name '*.c' -o -name '*.h' \)) --config-file=.clang-tidy --warnings-as-errors="*"
```

The repository-local `make lint` target runs the same commands across every C,
C++, and header file under `src/`, `tests/`, and `user/`. User assembly fixtures
are compiled and embedded by the build but are not inputs to the C/C++ checks.

These checks must pass. When a check reports a diagnostic, first determine
whether it identifies a real defect. Fix real defects in the code. When a
diagnostic is intentional because of freestanding or architecture-specific
kernel code, suppress it at the narrowest appropriate scope: one line, a
logical block, or the whole file. Disable a check project-wide only when the
rule is consistently inappropriate for OScar, and document that decision in
`.clang-tidy`.

The clangd pass must use the generated `compile_commands.json` and the
freestanding `x86_64-unknown-none-elf` target. Review any source diagnostics;
clangd may also report internal code-action test failures, which are editor
feature errors rather than source lint diagnostics.

Keep C++ source formatted according to `.clang-format`. Assembly should retain
the existing GNU assembler style and is not passed through clang-format.

## Commenting complex code

Add comments when a branch, invariant, ordering constraint, or low-level
implementation choice is not obvious from the code itself. Explain why the
choice exists and what incorrect behavior it prevents—for example, why a
context switch is skipped when the selected thread is already current, or why
interrupts must remain disabled while scheduler queues are updated.

Do not add comments that merely restate straightforward code. Comments should
capture design decisions, assumptions, ownership rules, hardware constraints,
failure-rollback reasoning, or other context that would otherwise be lost when
the implementation is changed.

## API documentation requirement

Every new public function must have API documentation in its header file.
Document its purpose, required initialization order, important ownership or
calling constraints, and whether it returns. Update existing header comments
when a function's behavior changes. Keep implementation-only helpers private
to their source file and do not add unnecessary public declarations.

## Editing Markdown files

Update `README.md` when adding features, changing build, compilation, or
installation steps, or changing the repository structure. Keep its overview,
examples, and repository layout accurate for a new checkout.

Update `AGENTS.md` when the repository structure changes so its layout and
contributor guidance reflect the current project. Keep development rules,
tooling instructions, and documentation requirements here rather than
duplicating them across unrelated Markdown files.

## Kernel development expectations

- Preserve the Limine-to-`kmain()` boot flow unless a task specifically changes
  boot infrastructure.
- Prefer small, independently bootable milestones.
- Use serial output for early diagnostics because graphics and device drivers
  are not yet available.
- Keep architecture-specific code isolated and clearly named.
- Verify changes in QEMU before considering a kernel milestone complete.
