# Host builds on macOS / Apple Silicon

The host build of the C++ app (`sim/`, which `./dbt sim` and the Embassy host harnesses link) can build natively on
macOS as 64-bit (Apple Silicon → arm64 with native NEON; Intel Mac → x86-64 with SIMDe). The CMake **auto-detects**
macOS — no flags or toolchain file needed. The 64-bit core was proven on x86-64 Linux first (see
`docs/superpowers/specs/2026-07-04-host-sim-64bit-apple-silicon-design.md`).

> Status: the CMake is in place, but the macOS build has **not been compiled on a Mac yet**. This guide is for that
> first on-Mac run. Please report which step fails and the exact error.

## Prerequisites

```sh
xcode-select --install                      # ld64, SDK headers (NOT the compiler — see below)
brew install cmake ninja llvm                # build tools + mainline LLVM clang
rustup target add aarch64-apple-darwin       # the Rust BSP's host target (Apple Silicon)
# Intel Mac instead: rustup target add x86_64-apple-darwin
```

**Compiler: mainline LLVM clang, not AppleClang.** The host build is C++26, and Apple's bundled `AppleClang` tracks
Xcode rather than the mainline LLVM release — it lags far enough that the portable app's C++26 does not compile.
`brew install llvm` supplies a current clang; the CMake **auto-selects it** on macOS (via `brew --prefix llvm`,
falling back to the well-known Homebrew/MacPorts paths) before `project()`. You do **not** need to set `CC`/`CXX` —
but if you do, or pass `-DCMAKE_CXX_COMPILER=…`, that choice is respected. If no mainline clang is found, configure
**fails** with a message telling you to `brew install llvm`.

argon, etl and SIMDe (Intel only) are fetched automatically (FetchContent).

## Build

`./dbt sim` and `./dbt test` build the tree themselves (`build/host-app`). To build the libraries alone:

```sh
cmake -B build-sim-mac -S sim -G Ninja
cmake --build build-sim-mac --target deluge_app deluge_scenarios
```

The auto-detection selects, on Apple Silicon:

- native 64-bit,
- **native aarch64 NEON via argon** — no SIMDe (`DELUGE_SIM_NEON_NATIVE`),
- `-dead_strip` instead of `--gc-sections`,
- mainline LLVM clang auto-selected for C++26 (with `-fconstexpr-steps` for the `validateParams()`
  `static_assert`, and its libc++ on the link rpath).

## Already handled

- **Mach-O section attributes** — `PLACE_SDRAM_*` used ELF-style `.sdram_bss` names that Mach-O rejects; they are
  no-ops on `__APPLE__` now (`src/board_config.h`).
- **`AppleClang` vs `Clang`** — the clang-only CMake block (`-fansi-escape-codes`, `-fconstexpr-steps`) matches both
  via `MATCHES "Clang"`. The libc++ link/rpath is gated to `STREQUAL "Clang"` so it only applies to the mainline
  clang, never AppleClang.

## Watch items (likely first failures, untested off-Mac)

1. **NE10 on aarch64.** NE10's int32 FFT is compiled from its NEON-intrinsic `.c/.cpp` (the armv7 `.s` files are
   excluded). aarch64 NEON is a superset of armv7 NEON, so it *should* compile, but watch for any intrinsic that
   doesn't exist on aarch64. If it breaks, the fallback is to route NE10 through SIMDe on this path too (add the
   compat shim to the NE10 target and drop `DELUGE_SIM_NEON_NATIVE` for NE10 only).
2. **`<execution>` / parallel STL under libc++.** The `_GLIBCXX_USE_TBB_PAR_BACKEND=0` define is a libstdc++ macro
   and inert on libc++; if `std::execution` fails to resolve, we may need a libc++-specific guard where the app uses
   parallel algorithms.

## Running the emulator (`dbt sim`)

`dbt sim` runs the firmware on the Embassy host in deluge-sdk's desktop simulator (see
[host_emulator.md](host_emulator.md)). It has not been run on macOS: the simulator window is a wgpu/winit app
(Metal there), its audio is cpal (CoreAudio) and its MIDI ports are midir's virtual ports (CoreMIDI), so each has a
macOS backend, but none is tested.
