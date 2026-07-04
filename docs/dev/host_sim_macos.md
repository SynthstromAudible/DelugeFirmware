# Host-sim on macOS / Apple Silicon

The host-sim can build and run natively on macOS as a 64-bit binary (Apple Silicon → arm64
with native NEON; Intel Mac → x86-64 with SIMDe). The sim CMake **auto-detects** macOS — no
flags or toolchain file needed. This is the Phase 2 target of the host-sim 64-bit work; the
64-bit core was proven on x86-64 Linux first (see
`docs/superpowers/specs/2026-07-04-host-sim-64bit-apple-silicon-design.md`).

> Status: the CMake + one source fix are in place and the Linux modes still build, but the
> macOS build has **not been compiled on a Mac yet**. This guide is for that first on-Mac run.
> Please report which step fails and the exact error.

## Prerequisites

```sh
xcode-select --install                      # ld64, SDK headers (NOT the compiler — see below)
brew install cmake ninja llvm                # build tools + mainline LLVM clang
rustup target add aarch64-apple-darwin       # Rust staticlib target (Apple Silicon)
# Intel Mac instead: rustup target add x86_64-apple-darwin
```

**Compiler: mainline LLVM clang, not AppleClang.** The sim builds at C++26, and Apple's
bundled `AppleClang` tracks Xcode rather than the mainline LLVM release — it lags far enough
that the portable app's C++26 does not compile. `brew install llvm` supplies a current clang;
the sim CMake **auto-selects it** on macOS (via `brew --prefix llvm`, falling back to the
well-known Homebrew/MacPorts paths) before `project()`. You do **not** need to set `CC`/`CXX` —
but if you do, or pass `-DCMAKE_CXX_COMPILER=…`, that choice is respected. If no mainline clang
is found, configure **fails** with a message telling you to `brew install llvm`.

argon, etl, SIMDe (Intel only), CppSpec and the Rust crates are fetched automatically
(FetchContent / cargo).

## Build

```sh
cmake -B build-sim-mac -S sim -G Ninja
cmake --build build-sim-mac --target deluge_host deluge_render deluge_loadcheck
```

The auto-detection selects, on Apple Silicon:

- native 64-bit (no `-m32`, no `-static-libgcc`),
- **native aarch64 NEON via argon** — no SIMDe (`DELUGE_SIM_NEON_NATIVE`),
- `-dead_strip` instead of `--gc-sections`,
- mainline LLVM clang auto-selected for C++26 (with `-fconstexpr-steps` for the
  `validateParams()` `static_assert`, and its libc++ on the link rpath),
- Rust target `aarch64-apple-darwin`.

Do **not** build the `deluge_host_boundary` target — it is a stale Stage-1 smoke target that
fails to link (`undefined deluge_main`) on every platform, Linux included. Build the three
real targets named above.

## Already handled

- **Mach-O section attributes** — `PLACE_SDRAM_*` used ELF-style `.sdram_bss` names that
  Mach-O rejects; they are no-ops on `__APPLE__` now (`src/board_config.h`).
- **`AppleClang` vs `Clang`** — the clang-only CMake block (`-fansi-escape-codes`,
  `-fconstexpr-steps`, static-libgcc) matches both via `MATCHES "Clang"`. The libc++
  link/rpath is gated to `STREQUAL "Clang"` so it only applies to the mainline clang, never
  AppleClang.
- **librt** — `find_library(rt)` returns NOTFOUND on macOS (librt is in libSystem) and the
  `if(RT_LIB)` guards already skip linking it.

## Watch items (likely first failures, untested off-Mac)

1. **NE10 on aarch64.** NE10's int32 FFT is compiled from its NEON-intrinsic `.c/.cpp` (the
   armv7 `.s` files are excluded). aarch64 NEON is a superset of armv7 NEON, so it *should*
   compile, but watch for any intrinsic that doesn't exist on aarch64. If it breaks, the
   fallback is to route NE10 through SIMDe on this path too (add the compat shim to the NE10
   target and drop `DELUGE_SIM_NEON_NATIVE` for NE10 only).
2. **`<execution>` / parallel STL under libc++.** The `_GLIBCXX_USE_TBB_PAR_BACKEND=0` define
   is a libstdc++ macro and inert on libc++; if `std::execution` fails to resolve, we may need
   a libc++-specific guard where the app uses parallel algorithms.
3. **`clock_gettime(CLOCK_MONOTONIC)`** (host_audio.c / host_bsp.c) — available on macOS ≥10.12,
   so fine on Apple Silicon, but flag it if the SDK complains.
4. **The Rust staticlib link** — if the link fails on `libdeluge_rust.a`, confirm
   `rustup target add aarch64-apple-darwin` ran and `crates/…/aarch64-apple-darwin/release/`
   was produced.

## Running the emulator (`dbt sim`)

`dbt sim` builds `deluge_host` (via the CMake above) **and** the `deluge-simulator` front-panel
GUI (a Rust/wgpu crate fetched from the deluge-sdk repo; wgpu uses Metal on macOS), then launches
both wired over a Unix socket. On Apple Silicon:

- **Audio just works** — `host_pcm.c` has a native CoreAudio (AudioQueue) backend that is the
  default on macOS (no PipeWire/`pw-cat`, no `--audio` flag, no extra install). `-framework
  AudioToolbox` is linked automatically.
- **MIDI** — the serial-MIDI auto-bridge looks for an ALSA `snd-virmidi` device, which macOS
  doesn't have; the auto-detect returns NULL and the bridge simply no-ops (not fatal). A CoreMIDI
  virtual-source bridge would be the macOS equivalent — not yet implemented.
- The `deluge-simulator` GUI build from deluge-sdk is untested on macOS but is a standard
  wgpu/winit app (Metal backend), so it should build.

## Verify (once it builds)

```sh
./build-sim-mac/deluge_render          # prints usage → links + runs
file build-sim-mac/deluge_render       # → arm64 (Apple Silicon)
```

Then, with a Deluge Backup + the golden cache present (same as on Linux):

```sh
export BUILD_DIR=$PWD/build-sim-mac DELUGE_BACKUP=~/"Deluge Backup"
FIXTURE=cordae     NO_BUILD=1 scripts/golden_mixdown.sh padsweep   # 64-bit layout invariance
FIXTURE=cordae     NO_BUILD=1 scripts/golden_mixdown.sh check      # vs stored golden
```

Expected, consistent with the Phase 1 (x86-64) findings:

- **padsweep PASS** on every fixture (the port has no address dependence).
- **cordae** (cache-free) should be a strong candidate to match the **x86-64 render**
  byte-for-byte (both are 64-bit; native aarch64 NEON vs SIMDe should be bit-identical for the
  ops used) — a good cross-arch sanity check. Copy an x86-64 `cordae` render over and `cmp`.
- **cache-heavy songs** (highsiderr, icoustic) will differ from the 32-bit golden — expected,
  because 64-bit pointer width enlarges cache-metadata structs and shifts eviction timing.
  That is why `-m32` stays the golden/CI reference (coexist), not this build.
