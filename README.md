# bdt

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

```sh
make
```

or from the CLeonOS root:

```sh
make bdt
```

## Usage

From a project root with `project.bdt`:

```sh
build/bdt/bdt --list
build/bdt/bdt iso -j 4
```

When used as a submodule in CLeonOS, the root `Makefile` bootstraps bdt and
passes configured toolchain variables automatically.

## License

Apache License 2.0. See `LICENSE`.
