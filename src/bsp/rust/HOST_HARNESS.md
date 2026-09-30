# The BSP on the host

`deluge-bsp-rust` builds for the device (`./dbt build`) and for the host (plain
`cargo build`/`cargo test`). The host builds:

| Build | Features | What it is |
|---|---|---|
| plain host | (default) | The BSP without the C++ app: a boot smoke of the control and display tasks, and the library the host tests import. `sys` is the hand-written stand-in `src/abi/sys_host.rs`. |
| threaded host app | `host_app` | The real C++ app, built by CMake into the host tree, on three executor threads (audio, storage, app). `sys` is bindgen's output. Optional harness modes (the streaming scenario, the recorder probes) run from environment variables; see `src/runtime/host.rs`. |
| emulator | `sim` | The threaded host app as the brain of deluge-sdk's desktop simulator (`./dbt sim`). |
| golden renderer | — | `harness/golden_vt_render`: the library under its own executor and virtual clock. It depends on this crate without `host-runtime`. |

`host_app` links the objects of the CMake host tree, `build/host-app` by default
(`DELUGE_HOSTAPP_BUILD_DIR` overrides it). `./dbt test --build-only golden-vt-render` builds that
tree. The build script never runs CMake; `build/host_app.rs` explains what it checks before it
links.

## ThreadSanitizer

`harness/preemptive_race_tsan/run.sh` does all of this and classifies what TSan reports against
`races.txt`; the steps are here for running it by hand.

### One-time setup

The TSan build uses a custom target spec, `sanitizer/x86_64-unknown-linux-gnu-tsan.json`: a copy
of `x86_64-unknown-linux-gnu` under another name. With `-Zbuild-std`, a target triple equal to the
host's makes Cargo build two `core`s for one crate (`error[E0152]: duplicate lang item`), because
the build script's `bindgen` dependency graph overlaps the target's. The custom target has no
prebuilt sanitizer runtime, so link it to the real one:

```sh
SYSROOT=$(rustc +nightly --print sysroot)
mkdir -p "$SYSROOT/lib/rustlib/x86_64-unknown-linux-gnu-tsan/lib"
ln -sf "$SYSROOT/lib/rustlib/x86_64-unknown-linux-gnu/lib/librustc-nightly_rt.tsan.a" \
       "$SYSROOT/lib/rustlib/x86_64-unknown-linux-gnu-tsan/lib/librustc-nightly_rt.tsan.a"
```

`-Zbuild-std` is not optional: without it `std`'s own locks are uninstrumented, and correctly
locked code in `critical-section` and `embassy-time` reports as racing.

### Build the C++ app with TSan

The C++ side must be compiled by a clang whose LLVM major matches `rustc +nightly`'s: the one TSan
runtime in the binary is rustc's (LLVM compiler-rt), and objects instrumented for another runtime
are not compatible with it. Compare `clang++ --version` with
`rustc +nightly --version --verbose | grep LLVM`.

```sh
# From the repository root, into a tree of its own:
cmake -B build-embassy-hostapp-tsan -S sim -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-fsanitize=thread" -DCMAKE_CXX_FLAGS="-fsanitize=thread"
ninja -C build-embassy-hostapp-tsan deluge_app deluge_scenarios NE10 eyalroz_printf \
  deluge_dsp deluge_foundation deluge_midi
```

### Build and run the host app against it

```sh
# From src/bsp/rust:
DELUGE_HOSTAPP_BUILD_DIR=$PWD/../../../build-embassy-hostapp-tsan \
RUSTFLAGS="-Zsanitizer=thread" TSAN_OPTIONS="halt_on_error=0" \
cargo +nightly run --features host_app \
  -Zbuild-std=core,alloc,std,panic_abort -Zjson-target-spec \
  --target sanitizer/x86_64-unknown-linux-gnu-tsan.json
```

`halt_on_error=0` reports every race in one run rather than stopping at the first.

Switching `DELUGE_HOSTAPP_BUILD_DIR` between the plain and the TSan tree relinks by itself: the
build script tracks the variable and threads a hash of the archived objects into the build (see
`build/link.rs`). `run.sh` still deletes `target/x86_64-unknown-linux-gnu-tsan` before each build,
which is cheap insurance when the C++ side is changing under you.

A clean run means TSan saw no race in that schedule, not that none exists.
