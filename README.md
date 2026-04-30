# bdt

[中文](README.zh-CN.md)

bdt (Build Tool) is a small C build tool for CLeonOS and other freestanding or
native projects. It reads project configuration files, resolves targets and
dependencies, runs toolchain commands, and emits structured build logs.

## Features

- Target-based build graph.
- Command, group, compile, link, archive-style application, tar, truncate, and
  cleanup targets.
- Configurable GCC/G++/LD/Rust toolchain variables.
- Incremental-cache primitives based on hashing.
- ANSI-colored logs and compile progress output.
- Chinese and English message support.
- C application builder support for CLeonOS user programs.

## Build

Build bdt itself:

```sh
make
```

or from the CLeonOS root:

```sh
make bdt
```

The executable is written to `build/bdt` by the standalone Makefile, or to
`build/bdt/bdt` when bootstrapped by the CLeonOS root Makefile.

## Quick Start

Create a `project.bdt` in your project root:

```ini
[project]
name = Example
default = app
build_dir = build
language = en

[tools]
CC = gcc
LD = ld

[config]
CFLAGS = -std=c11,-Wall,-Wextra

[target.app-objects]
type = compile
tool = {CC}
sources = src/main.c, src/util.c
output = {root}/build/obj
cflags = {CFLAGS}

[target.app]
deps = app-objects
type = link
tool = {LD}
inputs = {root}/build/obj
output = {root}/build/example.elf
ldflags =
```

Then run:

```sh
build/bdt/bdt --list
build/bdt/bdt app -j 4
```

## Project File

bdt reads `project.bdt` from the project root by default. The file uses an INI
style format.

### Project Section

```ini
[project]
name = Example
default = app
build_dir = build
language = en
```

- `name`: displayed in target lists and logs.
- `default`: target used when no target is passed.
- `build_dir`: project build directory variable.
- `language`: `en`, `zh`, or `zh-CN`.

### Tools Section

```ini
[tools]
CC = gcc
KERNEL_CXX = g++
LD = ld
RUSTC = rustc
NM = nm
TAR = tar
```

Tool values can be overridden with environment variables:

```sh
CC=x86_64-elf-gcc LD=x86_64-elf-ld build/bdt/bdt kernel
```

### Config Section

`[config]` stores reusable variables:

```ini
[config]
CFLAGS = -std=c11,-ffreestanding,-Wall,-Wextra
LDFLAGS = -nostdlib
```

Use variables as `{NAME}` in target fields. bdt also provides `{root}`,
`{project}`, and `{build_dir}`.

Lists use commas in config files. bdt expands commas to spaces when a field is
used as command flags.

## Target Types

Every target is declared as `[target.NAME]`.

### command

Runs one shell command.

```ini
[target.configure]
type = command
command = python3 scripts/configure.py
always = true
cache = false
```

### group

Runs multiple shell steps separated by `|`.

```ini
[target.prepare]
type = group
steps = mkdir -p "{root}/build" | cp config.ini "{root}/build/config.ini"
cache = false
```

### compile

Compiles source files to object files and shows progress.

```ini
[target.objects]
type = compile
tool = {CC}
sources = src/main.c, src/io.c
output = {root}/build/obj
cflags = {CFLAGS}
```

### link

Links object files. If `inputs` points to a directory, bdt collects `.o` files
recursively.

```ini
[target.app]
deps = objects
type = link
tool = {LD}
inputs = {root}/build/obj
output = {root}/build/app.elf
ldflags = {LDFLAGS}
```

### rust-staticlib

Builds a Rust static library.

```ini
[target.rustlib]
type = rust-staticlib
tool = {RUSTC}
sources = rust/src/lib.rs
output = {root}/build/lib/libexample.a
flags = --crate-type,staticlib,-C,panic=abort,-O
```

### tar

Creates a tar archive from a directory.

```ini
[target.ramdisk]
type = tar
tool = {TAR}
inputs = {root}/ramdisk_root
output = {root}/build/ramdisk.tar
```

### truncate

Creates a zero-filled image file.

```ini
[target.disk-image]
type = truncate
output = {root}/build/disk.img
flags = 64
```

### remove

Removes files or directories listed in `inputs`.

```ini
[target.clean]
type = remove
inputs = {root}/build
always = true
cache = false
```

### c-apps

Builds CLeonOS-style user applications from `*_main.c` and `*_kmain.c` files.
This target type is useful for CLeonOS and can also serve as a template for
project-specific application builders.

```ini
[target.userapps]
type = c-apps
tool = {CC}
main_dir = {root}/cleonos/c/apps
common_dirs = {root}/cleonos/c/src
runtime_sources = {root}/cleonos/c/apps/cmd_runtime.c
objects = {root}/build/x86_64/user/obj
output = {root}/build/x86_64/user/apps
system_output = {root}/build/x86_64/user/system
linker_script = {root}/cleonos/c/user.ld
system_linker_script = {root}/cleonos/c/kelf.ld
cflags = {USER_CFLAGS}
ldflags = {USER_LDFLAGS}
app.browser.cflags = {USER_TLS_CFLAGS},{USER_GUMBO_CFLAGS}
app.browser.sources = {USER_TLS_SOURCES}
```

Per-application fields:

- `app.NAME.cflags`: extra flags for one app.
- `app.NAME.sources`: extra source files for one app.
- `app.NAME.source_dirs`: extra recursive source directories.
- `app.NAME.exclude_sources`: source basenames or relative paths to skip.

## Common Commands

From a project root with `project.bdt`:

```sh
build/bdt/bdt --list
build/bdt/bdt --scan
build/bdt/bdt --graph
build/bdt/bdt iso -j 4
```

Useful flags:

- `--list`: list known targets.
- `--scan`: list discovered build descriptor files.
- `--graph`: print target dependencies.
- `--verbose`: print shell commands before running them.
- `-j N`: set job count. Current target types may still run serially unless
  they implement parallel execution.
- `--no-cache`: ignore cache checks.

## CLeonOS Usage

When used as a submodule in CLeonOS, the root `Makefile` bootstraps bdt and
passes configured toolchain variables automatically.

```sh
make bdt
make menuconfig
make kernel
make userapps
make iso
make run
```

For CI or reproducible builds:

```sh
make menuconfig MENUCONFIG_PRESET=full MENUCONFIG_ARGS="--defaults --non-interactive"
make iso CC=gcc KERNEL_CXX=g++ LD=ld NM=nm JOBS="$(nproc)"
```

## License

Apache License 2.0. See `LICENSE`.
