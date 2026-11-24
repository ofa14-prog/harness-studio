# Harness Studio

Harness Studio is a customized Blender distribution featuring a Qt 6 GHOST backend, Qt Quick shell, and an integrated Go launcher runtime.

## Project Structure

- `blender/`: Blender source tree with Qt 6 GHOST backend (`intern/ghost`) and Harness Studio shell (`intern/harness`).
- `app/`: Lightweight launcher application and build scripts.
- `deps/`: External dependencies including Eigen3.
- `tools/`: Utility scripts and render presets.
- `MECHANICS.md`: Technical documentation of the architecture and interface.

## Prerequisites

- CMake 3.20+
- Ninja
- GCC/Clang with C++20 support
- Qt 6 (Declarative / Quick)
- Go 1.17+ (for the launcher)
- Python 3.10+

## Building

To configure and build Harness Studio:

```sh
make
```

To build only the Go launcher executable:

```sh
make build-launcher
```

## Running

Launch Harness Studio:

```sh
./run.sh
```

Or using the launcher directly:

```sh
go run ./app
```
