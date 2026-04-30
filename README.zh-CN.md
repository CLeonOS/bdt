# bdt

[English](README.md)

bdt (Build Tool) 是一个用 C 编写的小型构建工具，面向 CLeonOS，也可用于
其他 freestanding 或普通 C/C++ 项目。bdt 读取项目根目录中的
`project.bdt`，解析 target、依赖关系、工具链变量，并输出结构化构建日志。

## 功能

- 基于 target 的构建图。
- 支持 command、group、compile、link、Rust staticlib、tar、truncate、
  remove、C 应用构建等 target 类型。
- GCC/G++/LD/Rust 工具链可配置。
- 基于 hash 的增量缓存基础能力。
- ANSI 彩色日志和编译进度条。
- 中文和英文日志。
- 支持 CLeonOS 用户态应用构建规则。

## 构建 bdt

单独构建 bdt：

```sh
make
```

在 CLeonOS 根目录中构建：

```sh
make bdt
```

单独构建时产物位于 `build/bdt`；作为 CLeonOS 子仓库使用时，根 Makefile
会生成 `build/bdt/bdt`。

## 快速开始

在项目根目录创建 `project.bdt`：

```ini
[project]
name = Example
default = app
build_dir = build
language = zh-CN

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

然后运行：

```sh
build/bdt/bdt --list
build/bdt/bdt app -j 4
```

## project.bdt 格式

bdt 默认读取项目根目录下的 `project.bdt`。文件格式类似 INI。

### project 段

```ini
[project]
name = Example
default = app
build_dir = build
language = zh-CN
```

- `name`：项目名称，用于 target 列表和日志。
- `default`：未指定 target 时使用的默认目标。
- `build_dir`：构建目录变量。
- `language`：`en`、`zh` 或 `zh-CN`。

### tools 段

```ini
[tools]
CC = gcc
KERNEL_CXX = g++
LD = ld
RUSTC = rustc
NM = nm
TAR = tar
```

工具变量可以通过环境变量覆盖：

```sh
CC=x86_64-elf-gcc LD=x86_64-elf-ld build/bdt/bdt kernel
```

### config 段

`[config]` 用于定义可复用变量：

```ini
[config]
CFLAGS = -std=c11,-ffreestanding,-Wall,-Wextra
LDFLAGS = -nostdlib
```

在 target 字段中使用 `{NAME}` 引用变量。bdt 内置 `{root}`、`{project}`、
`{build_dir}`。

配置文件中的列表使用逗号分隔。作为命令 flags 使用时，bdt 会把逗号展开为空格。

## Target 类型

每个 target 使用 `[target.NAME]` 声明。

### command

运行单条 shell 命令。

```ini
[target.configure]
type = command
command = python3 scripts/configure.py
always = true
cache = false
```

### group

运行多条 shell 步骤，步骤之间用 `|` 分隔。

```ini
[target.prepare]
type = group
steps = mkdir -p "{root}/build" | cp config.ini "{root}/build/config.ini"
cache = false
```

### compile

把源码编译为对象文件，并显示进度。

```ini
[target.objects]
type = compile
tool = {CC}
sources = src/main.c, src/io.c
output = {root}/build/obj
cflags = {CFLAGS}
```

### link

链接对象文件。如果 `inputs` 是目录，bdt 会递归收集其中的 `.o` 文件。

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

构建 Rust static library。

```ini
[target.rustlib]
type = rust-staticlib
tool = {RUSTC}
sources = rust/src/lib.rs
output = {root}/build/lib/libexample.a
flags = --crate-type,staticlib,-C,panic=abort,-O
```

### tar

从目录创建 tar 包。

```ini
[target.ramdisk]
type = tar
tool = {TAR}
inputs = {root}/ramdisk_root
output = {root}/build/ramdisk.tar
```

### truncate

创建填零磁盘镜像。

```ini
[target.disk-image]
type = truncate
output = {root}/build/disk.img
flags = 64
```

### remove

删除 `inputs` 中列出的文件或目录。

```ini
[target.clean]
type = remove
inputs = {root}/build
always = true
cache = false
```

### c-apps

构建 CLeonOS 风格的用户态 C 应用。bdt 会扫描 `*_main.c` 和 `*_kmain.c`。
这个 target 类型主要服务 CLeonOS，也可以作为其他项目的应用构建器模板。

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

单个应用可配置字段：

- `app.NAME.cflags`：某个应用的额外编译参数。
- `app.NAME.sources`：某个应用额外源码。
- `app.NAME.source_dirs`：某个应用额外递归源码目录。
- `app.NAME.exclude_sources`：需要跳过的源码文件名或相对路径。

## 常用命令

在带有 `project.bdt` 的项目根目录运行：

```sh
build/bdt/bdt --list
build/bdt/bdt --scan
build/bdt/bdt --graph
build/bdt/bdt iso -j 4
```

常用参数：

- `--list`：列出 target。
- `--scan`：列出扫描到的构建描述文件。
- `--graph`：输出 target 依赖关系。
- `--verbose`：执行命令前打印完整 shell 命令。
- `-j N`：设置并行数。当前部分 target 类型仍可能串行执行。
- `--no-cache`：忽略缓存检查。

## 在 CLeonOS 中使用

bdt 作为 CLeonOS 的子仓库使用时，根目录 `Makefile` 会自动构建 bdt，并传入
工具链变量。

```sh
make bdt
make menuconfig
make kernel
make userapps
make iso
make run
```

CI 或可复现构建示例：

```sh
make menuconfig MENUCONFIG_PRESET=full MENUCONFIG_ARGS="--defaults --non-interactive"
make iso CC=gcc KERNEL_CXX=g++ LD=ld NM=nm JOBS="$(nproc)"
```

## 许可证

Apache License 2.0。见 `LICENSE`。
