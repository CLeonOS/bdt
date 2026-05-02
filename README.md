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
- File-level incremental compilation based on source, header dependency,
  flags, and toolchain-version hashes.
- ANSI-colored logs and compile progress output.
- Chinese and English message support.
- ncurses project structure viewer with targets, dependencies, plugins, cache,
  build files, and subprojects.
- `why`, `status`, and `trace` commands for build ownership, rebuild previews,
  and JSON build event traces.
- `bench` command for target, compile, command, and link timing reports.
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

Optional shared cache settings:

```ini
[cache]
path = {root}/{build_dir}/cache
archive = {root}/{build_dir}/bdt-cache.tar
pull_command = aws s3 cp s3://bucket/project-cache.tar {root}/{build_dir}/bdt-cache.tar && bdt cache import
push_command = bdt cache export && aws s3 cp {root}/{build_dir}/bdt-cache.tar s3://bucket/project-cache.tar
```

Optional plugin runners:

```ini
[plugin.pack-image]
runner = python3
path = {root}/tools/pack_image.py

[target.image]
type = plugin
plugin = pack-image
inputs = {root}/build/rootfs
output = {root}/build/image.bin
flags = --format raw
```

Plugin runners receive `BDT_PROJECT_ROOT`, `BDT_PROJECT_NAME`, `BDT_TARGET`,
`BDT_PLUGIN`, `BDT_INPUTS`, `BDT_OUTPUTS`, `BDT_SOURCES`, `BDT_OUTPUT`, and
`BDT_FLAGS` in their environment.

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

Compiles source files to object files and shows progress. bdt writes GCC-style
`.d` dependency files and skips unchanged source files when the source hash,
header dependency hash, flags, and compiler version are unchanged.

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
recursively. bdt skips the link step when the object list, object contents,
linker script, linker flags, and linker version are unchanged.

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

Builds a directory of C applications from configurable entry-file suffixes.
This target type is generic: entry naming, runtime sources, output groups, and
per-application options are all controlled by `project.bdt`.

```ini
[target.userapps]
type = c-apps
tool = {CC}
main_dir = {root}/apps
common_dirs = {root}/lib
runtime_sources = {root}/runtime/app_runtime.c
entry_suffix = _main.c
secondary_entry_suffix = _service.c
secondary_output_group = services
runtime_exclude_apps = shell
objects = {root}/build/app-obj
output = {root}/build/apps
output_group.default.output = {root}/build/apps
output_group.services.output = {root}/build/services
output_group.tools.output = {root}/build/tools
output_group.tools.apps = editor, viewer
linker_script = {root}/link/app.ld
output_group.services.linker_script = {root}/link/service.ld
cflags = {APP_CFLAGS}
ldflags = {APP_LDFLAGS}
app.browser.cflags = {TLS_CFLAGS},{HTML_CFLAGS}
app.browser.sources = {TLS_SOURCES}
app.viewer.output_group = tools
app.shell.include_runtime = false
```

Per-application fields:

- `app.NAME.cflags`: extra flags for one app.
- `app.NAME.sources`: extra source files for one app.
- `app.NAME.source_dirs`: extra recursive source directories.
- `app.NAME.exclude_sources`: source basenames or relative paths to skip.
- `app.NAME.output_group`: output group for one app.
- `app.NAME.include_runtime`: whether to link shared runtime sources.

### plugin

Runs an external plugin target. A plugin can be a script, binary, or command
configured under `[plugin.NAME]`; bdt expands target fields and passes them as
environment variables.

```ini
[plugin.assets]
command = python3 {root}/tools/assets.py --in "$BDT_INPUTS" --out "$BDT_OUTPUT"

[target.assets]
type = plugin
plugin = assets
inputs = {root}/assets
output = {root}/build/assets.bin
```

## Common Commands

From a project root with `project.bdt`:

```sh
build/bdt/bdt --list
build/bdt/bdt --scan
build/bdt/bdt --graph
build/bdt/bdt view
build/bdt/bdt why src/main.c
build/bdt/bdt status app
build/bdt/bdt trace app --trace-out build/trace.json
build/bdt/bdt bench app
build/bdt/bdt iso -j 4
build/bdt/bdt explain iso
build/bdt/bdt clean kernel-objects
build/bdt/bdt doctor
build/bdt/bdt cache export
build/bdt/bdt cache import
build/bdt/bdt cache pull
build/bdt/bdt cache push
```

Useful flags:

- `--list`: list known targets.
- `--scan`: list discovered build descriptor files.
- `--graph`: print target dependencies.
- `view`: open an ncurses project structure viewer. It loads ncurses at runtime,
  so normal builds still work without ncurses; install `libncurses` if this
  command reports that ncurses is unavailable.
- `why <file|target>`: explain which target owns a target name or references a
  source, object, output, or configured path.
- `status [target]`: preview what would rebuild. For large `c-apps` targets this
  uses a fast cache approximation; the actual build still performs full hash
  validation.
- `trace [target] [--trace-out file]`: run a build and write a JSON event trace
  with target, command, compile/link cache, return code, and duration events.
- `bench [target] [--trace-out file]`: run a traced build and print slow target,
  compile, command, and link rankings. Without `--trace-out`, the trace is saved
  to `{build_dir}/bench-trace.json`.
- `--verbose`: print shell commands before running them.
- `-j N`: set job count. Current target types may still run serially unless
  they implement parallel execution.
- `--no-cache`: ignore cache checks.
- `explain <target>`: print target configuration and cache reasons.
- `clean <target>`: remove one target's known outputs and bdt cache metadata.
- `doctor`: check common tools, subprojects, output directory access, and
  target configuration problems.
- `cache export [archive]`: archive the local bdt cache.
- `cache import [archive]`: restore the local bdt cache from an archive.
- `cache pull` / `cache push`: run configured shared-cache hooks.

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
