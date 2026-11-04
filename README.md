# OpenSc3k — SimCity 3000 Unlimited reconstruction

An open-source C++23 reimplementation of SimCity 3000 Unlimited, built as **one executable** on top of SDL3.

> **Status: work in progress.** The game starts and loads a city from an installed copy, but it is not a complete
> replacement.

## You need your own copy of the game

This repository contains **no game assets**. OpenSc3k reads the resources of a legitimately installed
*SimCity 3000 Unlimited* (the folder that contains `Apps/` with `Sys/`, `Res/`, the `.DLL` files and so on).

## Build

Requirements: CMake 3.25+, a C++23 compiler (Clang, GCC or MSVC), Python 3 and network access for the first
configure (SDL3 and SDL3_ttf are fetched and linked statically).

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target sc3u
```

Builds are verified on macOS (arm64); Windows and Linux builds are supported by the same CMake project.

### Cross-compiling for Windows

A Windows x64 executable can be built from macOS or Linux with clang and a mingw-w64 sysroot (macOS: `brew install mingw-w64`):

```bash
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/windows-x64-clang-mingw.cmake
cmake --build build/windows --target sc3u
```

The result is a self-contained `OpenSc3k.exe` (no mingw runtime DLLs needed).

## Run

```bash
build/release/OpenSc3k -w -intro:off
```

On first start OpenSc3k asks for the folder where the game is installed, and remembers the choice. To skip the
prompt, set the folder explicitly:

```bash
SC3_GAME_DIR=/path/to/SC3 build/release/OpenSc3k -w -intro:off
```

The folder may be the install root or its `Apps` directory. Original command-line switches are accepted;
`-w` runs windowed.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | The reconstruction, grouped by original module (`city`, `network`, `geometry`, `ui`, `audio`, …) |
| `third_party/` | Bundled IJG JPEG 6a codec |
| `scripts/` | `make_unity.py` and `unity_order.txt`, which assemble the single translation unit |
| `docs/` | Notes: [architecture](docs/architecture.md) |

See [CONTRIBUTING.md](CONTRIBUTING.md) for the coding conventions (naming prefixes, original-signature rules,
restricted C++23).

## Licensing

OpenSc3k is free software under the **GNU General Public License v3.0 or later** (see [LICENSE](LICENSE)).
Bundled and derived third-party code (IJG JPEG, declarations adapted from `sc3k-gzcom-dll`, notes from `sc3kre`)
is listed in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). SimCity 3000 is a trademark of its owners; this
project is not affiliated with or endorsed by them, and it contains no original game assets.
