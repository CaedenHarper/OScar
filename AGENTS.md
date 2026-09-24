# OScar contribution guide

## Project overview

OScar is a small educational x86-64 operating-system kernel written in
freestanding C++ with a small amount of assembly. Limine loads the kernel ELF,
and QEMU provides the primary development and test environment. The kernel
currently initializes COM1 serial output, reads the Limine memory map, installs
an Interrupt Descriptor Table for CPU exceptions, reports exception register
state, and halts.

This is a bare-metal kernel rather than a hosted application. The normal C++
runtime, standard library, exceptions, RTTI, and operating-system services are
not available unless the project explicitly implements the required support.
Keep new code suitable for the freestanding build and avoid introducing
hosted-runtime assumptions.

## Repository layout

- `src/`: kernel C++ modules and architecture-specific assembly.
- `linker.ld`: higher-half kernel linker script and ELF entry point.
- `limine.conf`: Limine boot configuration.
- `Makefile`: kernel, ISO, QEMU, and exception-test targets.
- `deps/`: downloaded Limine dependencies; generated and ignored.
- `build/`: compiled objects, kernel ELF, and ISO output; generated and ignored.

The current source modules include serial output (`serial.*`), fatal error
handling (`panic.*`), exception handling (`idt.*` and `idt.S`), and physical
page allocation (`memory.*`), and virtual-memory mappings
(`virtual_memory.*`). Boot-time subsystem smoke tests live in
`self_tests.*` rather than in `main.cpp`.

## Build and test

Use the following checks before submitting changes:

```sh
make all
make run
make test-exception
```

`make run` should boot the kernel in headless QEMU and print serial output.
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
clang-format --dry-run --Werror src/*.cpp src/*.hpp
clang-tidy src/*.cpp --config-file=.clang-tidy
```

If a local environment does not provide `clang-format` or `clang-tidy`, at
minimum run `make all`, `git diff --check`, and `clangd --check=src/main.cpp`
when a usable compilation database is available. Do not treat the absence of a
linter executable as evidence that linting passed.

Keep C++ source formatted according to `.clang-format`. Assembly should retain
the existing GNU assembler style and is not passed through clang-format.

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
