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
- 基于源码、头文件依赖、编译参数和工具链版本 hash 的文件级增量编译。
- ANSI 彩色日志和编译进度条。
- 中文和英文日志。
- ncurses 项目结构查看器，可查看 targets、依赖、插件、缓存、构建文件和子项目。
- `why`、`status` 和 `trace` 命令，用于查看构建归属、重建预览和 JSON 构建事件追踪。
- `bench` 命令，用于输出 target、源码编译、命令和链接耗时排行。
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

可选的共享缓存配置：

```ini
[cache]
path = {root}/{build_dir}/cache
archive = {root}/{build_dir}/bdt-cache.tar
pull_command = aws s3 cp s3://bucket/project-cache.tar {root}/{build_dir}/bdt-cache.tar && bdt cache import
push_command = bdt cache export && aws s3 cp {root}/{build_dir}/bdt-cache.tar s3://bucket/project-cache.tar
```

可选的插件 runner：

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

插件 runner 会通过环境变量收到 `BDT_PROJECT_ROOT`、`BDT_PROJECT_NAME`、
`BDT_TARGET`、`BDT_PLUGIN`、`BDT_INPUTS`、`BDT_OUTPUTS`、`BDT_SOURCES`、
`BDT_OUTPUT` 和 `BDT_FLAGS`。

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

把源码编译为对象文件，并显示进度。bdt 会生成 GCC 风格的 `.d` 依赖文件；
当源码 hash、头文件依赖 hash、编译参数和编译器版本都未变化时，会跳过该
源码文件。

```ini
[target.objects]
type = compile
tool = {CC}
sources = src/main.c, src/io.c
output = {root}/build/obj
cflags = {CFLAGS}
```

### link

链接对象文件。如果 `inputs` 是目录，bdt 会递归收集其中的 `.o` 文件。当
对象列表、对象文件内容、链接脚本、链接参数和链接器版本都未变化时，bdt
会跳过链接。

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

按照可配置的入口文件后缀构建一个目录中的 C 应用。这个 target 类型是通用的：
入口命名、运行时源码、输出分组和单个应用选项都由 `project.bdt` 控制。

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

单个应用可配置字段：

- `app.NAME.cflags`：某个应用的额外编译参数。
- `app.NAME.sources`：某个应用额外源码。
- `app.NAME.source_dirs`：某个应用额外递归源码目录。
- `app.NAME.exclude_sources`：需要跳过的源码文件名或相对路径。
- `app.NAME.output_group`：指定某个应用输出到哪个输出分组。
- `app.NAME.include_runtime`：是否链接共享运行时源码。

### plugin

运行外部插件 target。插件可以是脚本、可执行文件或命令，在 `[plugin.NAME]`
中配置；bdt 会展开 target 字段，并通过环境变量传给插件。

```ini
[plugin.assets]
command = python3 {root}/tools/assets.py --in "$BDT_INPUTS" --out "$BDT_OUTPUT"

[target.assets]
type = plugin
plugin = assets
inputs = {root}/assets
output = {root}/build/assets.bin
```

## 常用命令

在带有 `project.bdt` 的项目根目录运行：

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

常用参数：

- `--list`：列出 target。
- `--scan`：列出扫描到的构建描述文件。
- `--graph`：输出 target 依赖关系。
- `view`：打开 ncurses 项目结构查看器。它在运行时加载 ncurses，所以普通构建
  不依赖 ncurses；如果该命令提示 ncurses 不可用，安装 `libncurses` 即可。
- `why <file|target>`：解释某个 target 名称，或某个源码、对象、输出、配置路径
  被哪个 target 引用。
- `status [target]`：预览本轮会重建什么。大型 `c-apps` target 会使用快速缓存
  近似判断；真正构建仍然执行完整 hash 校验。
- `trace [target] [--trace-out file]`：执行一次构建，并写出 JSON 事件追踪，包含
  target、command、compile/link cache、返回码和耗时。
- `bench [target] [--trace-out file]`：执行一次带 trace 的构建，并输出慢 target、
  慢源码编译、慢命令和慢链接排行。未指定 `--trace-out` 时，trace 写到
  `{build_dir}/bench-trace.json`。
- `--verbose`：执行命令前打印完整 shell 命令。
- `-j N`：设置并行数。当前部分 target 类型仍可能串行执行。
- `--no-cache`：忽略缓存检查。
- `explain <target>`：输出 target 配置和缓存判断原因。
- `clean <target>`：只删除某个 target 的已知产物和 bdt 缓存元数据。
- `doctor`：检查常见工具、子项目、输出目录权限和 target 配置问题。
- `cache export [archive]`：把本地 bdt 缓存打包成归档。
- `cache import [archive]`：从归档恢复本地 bdt 缓存。
- `cache pull` / `cache push`：执行配置好的共享缓存钩子。

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
