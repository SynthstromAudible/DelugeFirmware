# libdeluge ABI P3: the header is the contract — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the compiler check every Rust export's signature, and every constant the Rust side copies, against
the libdeluge headers. Delete what exists only because several platforms once implemented the ABI: the
`*_async_active` probes, the two storage feature flags, and the `*_abi.cpp` pin files.

**Architecture:**
- The headers share one `abi.h` (`DELUGE_NOEXCEPT`) and one `channel.h` (the channel status, `DELUGE_PATH_MAX`).
- bindgen reads every header and emits functions and constants as well as types.
- An `abi_pin!` macro puts each Rust export beside its generated declaration in a two-element array. A different
  signature fails to compile with E0308. This was checked with rustc on 2026-09-29, for a parameter type, a return
  type and a pointer's mutability, with safe and unsafe exports.
- An `abi_const!` macro asserts that each copied constant equals the generated one.
- `scripts/check_abi_surface.py --pins` fails when a Rust-defined, declared function has no pin.

**Tech Stack:** bindgen (`src/bsp/rust/build.rs`), Rust macros, C headers, Python (the surface script from P2).

**Spec:** `docs/superpowers/specs/2026-09-29-libdeluge-abi-overhaul-design.md` (§2 B2, C1, C2, C4, D1, D2, D3, F2;
§3 decisions 5, 6 and 9)

## Global Constraints

- Branch off `next` after P2 has merged: `git switch -c chore/libdeluge-header-contract next`. Local merges go
  into `next`, never `main`. Never push.
- Commit after every task, without asking. No `Co-Authored-By`. Commit subjects name the change, not the process.
- If a pre-commit hook reformats files, re-add them and commit again. Never `--amend`.
- Comments state the current rationale only.
- Don't touch the website (`website/`).
- **Device link check:**
  ```bash
  mv src/bsp/rust/local-sdk.toml /tmp/local-sdk.toml.held 2>/dev/null; ./dbt build RelWithDebInfo; rc=$?; mv /tmp/local-sdk.toml.held src/bsp/rust/local-sdk.toml 2>/dev/null; git checkout -q src/bsp/rust/Cargo.lock; exit $rc
  ```
- **Host gates:** `./dbt test unit bsp-host crates golden-vt-render`. The `host_app` build runs bindgen for
  x86-64, and the device link check runs it for ARMv7. The pins must hold on both: `c_char` is `i8` on one and `u8`
  on the other.
- Every neutering check (a deliberate break that must fail the build) is reverted before the commit.

## Review Focus

- **`c_char` differs between the two targets.** An export that spells a C string `*const u8` or `*const i8`
  passes on one target and fails on the other. Every task that adds pins runs both the host gates and the device
  link check.
- **A header that cannot be parsed as C.** bindgen reads the headers as C. A header with C++ outside
  `#ifdef __cplusplus` fails Task 3. The fix belongs in the header, which is a C ABI.
- **A constant that bindgen types differently from the copy.** For example `0u` becomes `u32` while the Rust copy
  is `u8`. `abi_const!` compares the values after widening both to `i64`, so a width difference does not fail it,
  but a value difference does. Tested in Task 5 by its neutering step.
- **An export pinned in one carrier and missing in another.** Device-only modules (`audio.rs`, `midi.rs`) and
  host-only ones (`host_link_stubs.rs`, `audio_host.rs`) both define ABI functions, and each must pin its own. The
  `--pins` check counts a name as pinned if any `abi_pin!` names it. Task 7 therefore runs the pins through both
  the device link check and the host gates.
- **The removed async probes' scenario markers.** Removing `asyncActive=1` from a scenario's summary must not
  leave a registry `markers` entry that now never prints. Task 2 updates `tests/registry.toml`.

---

### Task 1: `abi.h` and `channel.h`

Six headers each define and `#undef` their own `DELUGE_NOEXCEPT`. The channel headers each spell the same four
status values and their own path bound.

**Files:**
- Create: `include/libdeluge/abi.h`, `include/libdeluge/channel.h`
- Modify: `include/libdeluge/file_bytes.h`, `file_directory.h`, `file_prefix.h`, `file_write.h`, `audio_load.h`,
  `card.h`: every header that has `#define DELUGE_NOEXCEPT`. Find them with
  `grep -l 'define DELUGE_NOEXCEPT' include/libdeluge/*.h`.
- Modify: `src/deluge/storage/file_bytes_abi.cpp`, `file_directory_abi.cpp`: their path-equality `static_assert`s
  become `DELUGE_PATH_MAX` checks.

**Interfaces:**
- Produces:
  - `DELUGE_NOEXCEPT` (from `abi.h`);
  - `DelugeChannelStatus` (`typedef uint8_t`), with the macros `DELUGE_CHANNEL_PENDING` = 0u,
    `DELUGE_CHANNEL_READY` = 1u, `DELUGE_CHANNEL_UNKNOWN` = 2u and `DELUGE_CHANNEL_FAILED` = 3u;
  - `DELUGE_PATH_MAX` = 256u (from `channel.h`);
  - the per-channel `*_PENDING`/`*_READY`/`*_UNKNOWN`/`*_FAILED` and `*_MAX_PATH` macros, which become aliases of
    these.

- [ ] **Step 1: Write the headers**

`include/libdeluge/abi.h`:

```c
/*
 * Copyright © 2026 Synthstrom Audible Limited
 * (licence header as in types.h)
 */

/// libdeluge/abi.h — what every libdeluge header shares.
#ifndef LIBDELUGE_ABI_H
#define LIBDELUGE_ABI_H

/// Marks a declaration `noexcept` when the header is read as C++, and nothing when it is read as C. No function
/// behind this ABI throws or unwinds: the Rust side aborts on panic, and C++ callbacks catch at the boundary.
#ifdef __cplusplus
#define DELUGE_NOEXCEPT noexcept
#else
#define DELUGE_NOEXCEPT
#endif

#endif // LIBDELUGE_ABI_H
```

`include/libdeluge/channel.h`:

```c
/*
 * Copyright © 2026 Synthstrom Audible Limited
 * (licence header as in types.h)
 */

/// libdeluge/channel.h — the request/collect channels' shared vocabulary.
///
/// A channel takes a request, returns at once, and is polled for the answer. Every poll answers one of these.
#ifndef LIBDELUGE_CHANNEL_H
#define LIBDELUGE_CHANNEL_H

#include "abi.h"
#include "types.h"

/// What a poll found: one of the `DELUGE_CHANNEL_*` values. A byte, and the values are macros rather than enum
/// constants, so that bindgen evaluates them, and so do the per-channel aliases defined in terms of them.
typedef uint8_t DelugeChannelStatus;
/// The request is queued or running: poll again.
#define DELUGE_CHANNEL_PENDING 0u
/// The answer is here, and this poll took it.
#define DELUGE_CHANNEL_READY 1u
/// No request for this key and card is queued, running or answered: ask again. An answer from before a card
/// ejection is never collected; its poll answers this.
#define DELUGE_CHANNEL_UNKNOWN 2u
/// The request ran and failed, and this poll took the failure.
#define DELUGE_CHANNEL_FAILED 3u

/// The longest path any channel takes, INCLUDING the NUL. A longer path is refused, never truncated: a truncated
/// path names a different file, which may exist.
#define DELUGE_PATH_MAX 256u

#endif // LIBDELUGE_CHANNEL_H
```

- [ ] **Step 2: Point every header at them**

In each header that defines `DELUGE_NOEXCEPT`:
- replace the `#ifdef __cplusplus` / `#define DELUGE_NOEXCEPT noexcept` / `extern "C" {` / `#else` /
  `#define DELUGE_NOEXCEPT` / `#endif` block with `#include "abi.h"` (on its own line, before the `extern "C"`
  opener), then a plain `#ifdef __cplusplus` / `extern "C" {` / `#endif`;
- delete the trailing `#undef DELUGE_NOEXCEPT`;
- delete the doc paragraph that explains the `#undef`.

In each channel header (`file_bytes.h`, `file_directory.h`, `file_prefix.h`, `file_write.h`, `audio_load.h`),
include `channel.h` and redefine the four status macros as aliases, keeping their per-status doc comments:

```c
#define DELUGE_FILE_DIRECTORY_PENDING DELUGE_CHANNEL_PENDING
#define DELUGE_FILE_DIRECTORY_READY DELUGE_CHANNEL_READY
#define DELUGE_FILE_DIRECTORY_UNKNOWN DELUGE_CHANNEL_UNKNOWN
#define DELUGE_FILE_DIRECTORY_FAILED DELUGE_CHANNEL_FAILED
```

`audio_load.h` has no FAILED. Alias its three. Make `DELUGE_MAX_PATH`, `DELUGE_FILE_DIRECTORY_MAX_PATH` and
`DELUGE_FILE_BYTES_MAX_PATH` aliases of `DELUGE_PATH_MAX`, and delete the paragraphs that justify the separate
spellings ("Spelled here rather than left to…").

The aliases keep every C++ use compiling unchanged, because they expand to the same `u`-suffixed integers as before.

Check that bindgen evaluates an alias chain (Task 3 depends on it): after Task 3, `grep -c 'pub const
DELUGE_FILE_WRITE_PENDING' $(find src/bsp/rust/target -name libdeluge_sys.rs | head -1)` must print 1. If bindgen
drops aliases, pin the Rust copies against `DELUGE_CHANNEL_*` in Task 5 instead.

- [ ] **Step 3: Replace the equality asserts**

In `file_bytes_abi.cpp` and `file_directory_abi.cpp`, delete the `static_assert`s that compare one channel's path
bound with another's (`file_bytes_abi.cpp:31-32`, and the like in `file_directory_abi.cpp`). The aliases make
them true by construction. Keep the asserts that pin a value (`== 8192u`, the `DelugeStatus` numbers); Task 6
moves those.

- [ ] **Step 4: Build and gate**

Run: `./dbt test unit bsp-host golden-vt-render song-preview-ab settings-channel document-save-channel`
Expected: PASS.

Neutering check: add `#define DELUGE_NOEXCEPT` to the top of `abi.h`, above its guard, and run `./dbt test unit`.
Expected: FAIL, with a macro-redefinition error. This shows every header now takes the macro from `abi.h`. Remove
the line.

Then run the device link check. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add include/libdeluge src/deluge/storage/file_bytes_abi.cpp src/deluge/storage/file_directory_abi.cpp
git commit -m "refactor: give the libdeluge headers one noexcept spelling and one channel vocabulary"
```

---

### Task 2: Delete the `*_async_active` probes and the storage feature flags

Five functions answer a constant `true`: `deluge_audio_load_async_active`, `deluge_file_bytes_async_active`,
`deluge_file_directory_async_active`, `deluge_file_prefix_async_active` and `deluge_file_write_async_active`, plus
`deluge_streaming_async_active`. They exist so that a build without the Rust channels can take a synchronous arm,
and no such build exists. The cargo features `efatfs_streaming` and `async_streaming_loader` are on in every build.

**Files** (as found by `git grep -n '_async_active\|efatfs_streaming\|async_streaming_loader'`, outside `docs/`):
- Headers: `include/libdeluge/audio_load.h`, `file_bytes.h`, `file_directory.h`, `file_prefix.h`, `file_write.h`,
  `streaming_fill.h`
- Rust: `src/bsp/rust/src/audio_loader.rs`, `file_bytes.rs`, `file_directory.rs`, `file_prefix.rs`,
  `file_write.rs`, `streaming_loader.rs`, `card.rs`, `main.rs`, `efatfs_fs.rs`, `recorder_probe.rs`;
  `src/bsp/rust/Cargo.toml`; `src/bsp/rust/harness/golden_vt_render/{Cargo.toml,src/main.rs}`;
  `crates/deluge_audio_loader_host/Cargo.toml`; `crates/deluge_sample_fill/src/fill_logic.rs` and
  `tests/fill_context_host.rs`; `src/bsp/rust/tests/audio_loader_host.rs`, `file_prefix_host.rs`,
  `streaming_fill_host.rs`; `src/bsp/rust/harness/preemptive_race_tsan/run.sh`; `src/bsp/rust/linker/memory_rtt.x`
  (comment)
- C++: `src/deluge/processing/stem_export/stem_export.cpp`, `storage/audio/async_sample_load.cpp`,
  `storage/audio/audio_file_manager.cpp`, `storage/byte_channel.cpp`
- Tests: `tests/support/mocks/storage_channel_mocks.cpp`, `tests/unit/sample_reader/mock_streaming_fill.cpp`,
  `tests/component/differential/region_read_differential/tests/differential.rs`,
  `tests/component/differential/region_fill_differential/tests/host_end_to_end.rs`,
  `tests/component/differential/fs_differential/tests/efatfs_core.rs` and `fixtures/mk_fixture.sh`
- Scenarios: `tests/integration/scenarios/audio_load_ab_scenario.cpp`, `load_policy_scenario.cpp`,
  `recorder_roundtrip_scenario.cpp`, `settings_channel_scenario.cpp`, `song_preview_ab_scenario.cpp`
- Registry: `tests/registry.toml` (every `gates` text or `markers` list that names `asyncActive`)
- `src/deluge/util/boot.h` (C2: say that `deluge::boot::finished()` is the selector)

- [ ] **Step 1: The C++ callers take the channel path unconditionally**

At each of the four app call sites, read the branch. `if (deluge_x_async_active()) { channel } else { sync }`
becomes `{ channel }`, and `if (!deluge_x_async_active()) { sync; return; }` loses the guard and the sync body.
Delete a sync-arm helper only when nothing else calls it; check with `git grep -n <helper>`. Where the choice
between the blocking and channel paths still matters, it is `deluge::boot::finished()`, and that call stays.

Add to the doc comment of `finished()` in `src/deluge/util/boot.h`: "This is the one switch between the blocking
storage calls boot uses and the channels everything after boot uses. The channels are always present."

- [ ] **Step 2: The scenarios stop printing and checking the probe**

In each scenario, delete the `asyncActive=` field from its summary line and any check on it. Then update
`tests/registry.toml`:
- delete `asyncActive` from the `markers` lists;
- in `song-preview-ab`'s `gates`, replace "Also pins that the run genuinely went through the channel
  (SP-SUMMARY asyncActive=1);" with "Also pins that the run went through the channel (SP-SUMMARY counts its
  requests);", if SP-SUMMARY prints a request count, or with nothing if it does not. Read the scenario to decide.

Run: `git grep -n 'asyncActive' -- tests`
Expected: no output.

- [ ] **Step 3: Delete the probes, their mocks and their Rust definitions**

Delete the six declarations and doc comments, the six Rust definitions, and the mocks in
`storage_channel_mocks.cpp` and `mock_streaming_fill.cpp`. In `card.rs`, delete the no-op-sibling paragraph
(lines 34-44) that exists for a build without the loader. In the Rust host tests, delete the assertions on the
probes.

- [ ] **Step 4: Make the two features unconditional**

In `src/bsp/rust/Cargo.toml`, delete `efatfs_streaming` and `async_streaming_loader` from `default` and from
`[features]`, with their doc comments. Do the same in `golden_vt_render/Cargo.toml`. In
`crates/deluge_audio_loader_host/Cargo.toml`, drop `"efatfs_streaming"` from the `check-cfg` values.

Then rewrite every `cfg` that names them:
- `#[cfg(feature = "efatfs_streaming")]` and `#[cfg(feature = "async_streaming_loader")]` are deleted, so the
  item is unconditional;
- an item under `#[cfg(not(feature = …))]` is deleted;
- `cfg(any(all(target_os = "none", feature = "efatfs_streaming"), feature = "host_app"))` becomes
  `cfg(any(target_os = "none", feature = "host_app"))`;
- `cfg(all(X, feature = "efatfs_streaming"))` becomes `cfg(X)`.

In the shell and fixture scripts, drop `--features efatfs_streaming`/`async_streaming_loader` arguments.

Run: `git grep -n 'efatfs_streaming\|async_streaming_loader\|_async_active' -- ':!docs/superpowers' ':!website'`
Expected: no output.

- [ ] **Step 5: Run the gates**

Run: `./dbt test`
Expected: every entry PASS, apart from entries whose status is `unavailable`. This task touches most storage
paths, so run the whole suite.

Then run the device link check. Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add -A include src tests crates scripts
git commit -m "chore: delete the async-channel probes and the storage features every build turns on"
```

---

### Task 3: bindgen reads every header and emits functions and constants

**Files:**
- Modify: `src/bsp/rust/wrapper.h` (include every header)
- Modify: `src/bsp/rust/build.rs:106-126` (`run_bindgen`: allowlist functions and variables)
- Modify: `src/bsp/rust/harness/golden_vt_render/build.rs` and its wrapper (the same, if the harness bindgens its
  own copy; find it with `grep -n 'allowlist_type' src/bsp/rust/harness/*/build.rs`)
- Modify: any header that bindgen cannot parse as C

**Interfaces:**
- Produces: `crate::sys::deluge_*` extern declarations for every header function, and
  `crate::sys::DELUGE_*` constants, in the device and `host_app` carriers.

- [ ] **Step 1: Include everything**

`src/bsp/rust/wrapper.h`:

```c
/* bindgen input: the whole libdeluge C ABI. Parsed as C (no __cplusplus), so every declaration and POD type is
 * read and the C++-only helpers are skipped. Include dir: <repo>/include (and src/, for OSLikeStuff). */
#include <libdeluge/libdeluge.h>
```

Task 8 makes `libdeluge.h` include every header. Until then, list them here explicitly, one `#include
<libdeluge/X.h>` per header in `include/libdeluge` (`ls include/libdeluge/*.h`), including `scheduler.h`.
`scheduler.h` reaches `OSLikeStuff/scheduler_api.h`, so add `.clang_arg(format!("-I{}", repo_root.join("src").display()))`
in `run_bindgen`.

- [ ] **Step 2: Emit functions and constants**

In `run_bindgen`, after `.allowlist_type("Deluge.*")`:

```rust
        // Every function and constant too: `abi_pin!` and `abi_const!` check the Rust side against these.
        .allowlist_function("deluge_.*")
        .allowlist_function(
            "addRepeatingTask|addConditionalTask|ignoreForStats|getAverageRunTimeForTask|getSystemTime|runTask|yield|blockTask|unblockTask|isSDRoutineActive",
        )
        .allowlist_var("DELUGE_.*")
```

The second list is `scheduler_api.h`'s surviving functions after P2. Check it:

Run: `scripts/check_abi_surface.py | awk '$2=="scheduler_api.h"{print $1}'`
Expected: exactly those ten names. If not, use the list it prints.

- [ ] **Step 3: Build, and fix what bindgen rejects**

Run: `./dbt test bsp-host golden-vt-render`
Expected: PASS. If bindgen stops on a header:
- If the header has C++ outside `#ifdef __cplusplus` (a `namespace`, a class, a default argument,
  `static_assert` without the C spelling), move that C++ into a `#ifdef __cplusplus` block, or into a C++ header
  beside it when the app needs it. A libdeluge header is read by C.
- If bindgen reports a duplicate definition, two headers declare the same name. Keep the declaration in the
  header whose subject it is, and include that header from the other.

Run the device link check. Expected: exit 0. The device carrier runs bindgen for ARMv7.

- [ ] **Step 4: Commit**

```bash
git add src/bsp/rust/wrapper.h src/bsp/rust/build.rs src/bsp/rust/harness include/libdeluge
git commit -m "build: generate the whole libdeluge ABI, functions and constants included"
```

---

### Task 4: `abi_pin!` pins every BSP export

**Files:**
- Modify: `src/bsp/rust/src/main.rs` (define `abi_pin!` and `abi_const!` before the first `mod`)
- Modify: every file under `src/bsp/rust/src` with a `#[unsafe(no_mangle)]` or `#[unsafe(export_name = …)]`
  function that a header declares. From the export count: `audio.rs`, `audio_host.rs`, `audio_loader.rs`,
  `board.rs`, `boot.rs`, `card.rs`, `control.rs`, `cv_gate.rs`, `display.rs`, `efatfs_fs.rs`,
  `efatfs_host_shim.rs`, `fault.rs`, `ffi.rs`, `file_bytes.rs`, `file_directory.rs`, `file_prefix.rs`,
  `file_write.rs`, `flash.rs`, `host_link_stubs.rs`, `midi.rs`, `scheduler.rs`, `sd.rs`, `services.rs`,
  `signals.rs`, `streaming_loader.rs`, `tier.rs`, `tier_meter.rs`.

**Interfaces:**
- Produces:
  - `abi_pin!(name, …)`, for exports defined in the current module;
  - `abi_pin!(in path::to::module: name, …)`, for exports defined elsewhere;
  - `abi_const!(ours => SYS_NAME, …)`.
  - In carriers without bindgen output (the plain host tests, which use `sys_host.rs`), all three expand to
    nothing.

- [ ] **Step 1: Define the macros**

In `src/bsp/rust/src/main.rs`, before the first `mod` item (`macro_rules!` is textually scoped):

```rust
/// Fails the build unless each named export has exactly the signature bindgen generated from its header. The
/// export and the generated declaration go into one array, which compiles only if both coerce to the same `fn`
/// pointer type. A safe export matches an `unsafe` declaration; a different parameter, return type or pointer
/// mutability does not (E0308).
#[cfg(any(target_os = "none", feature = "host_app"))]
macro_rules! abi_pin {
    (in $($m:ident)::+ : $($f:ident),+ $(,)?) => { $(
        const _: () = { let _ = [crate::sys::$f, $($m)::+::$f]; };
    )+ };
    ($($f:ident),+ $(,)?) => { $(
        const _: () = { let _ = [crate::sys::$f, $f]; };
    )+ };
}
#[cfg(not(any(target_os = "none", feature = "host_app")))]
macro_rules! abi_pin {
    ($($t:tt)*) => {};
}

/// Fails the build unless each constant copied on the Rust side equals the header's. Both sides are widened to
/// `i64`, so a copy may use the width its module needs.
#[cfg(any(target_os = "none", feature = "host_app"))]
macro_rules! abi_const {
    ($($ours:expr => $theirs:ident),+ $(,)?) => { $(
        const _: () = assert!(($ours) as i64 == crate::sys::$theirs as i64, stringify!($theirs));
    )+ };
}
#[cfg(not(any(target_os = "none", feature = "host_app")))]
macro_rules! abi_const {
    ($($t:tt)*) => {};
}
```

- [ ] **Step 2: Pin one module, and watch a mismatch fail**

At the end of `src/bsp/rust/src/boot.rs`:

```rust
abi_pin!(deluge_boot_finished);
```

Neutering check: change `deluge_boot_finished`'s return type to `u8` (and its body to `u8::from(finished())`).
Run: `./dbt test golden-vt-render`
Expected: FAIL, `error[E0308]: mismatched types` at the `abi_pin!` line. Revert.

- [ ] **Step 3: Pin every module**

At the end of each module in the Files list, add one `abi_pin!` naming every header-declared function the module
exports. For a module compiled only on one target, the pin sits in that module, so it is checked on that target.
List a module's exports with:

Run: `grep -B3 -h 'pub \(unsafe \)\?extern "C" fn' src/bsp/rust/src/<module>.rs | grep -o 'fn [a-zA-Z_0-9]*' | cut -d' ' -f2`

Then keep the names that `scripts/check_abi_surface.py <names>` reports with a header, not `-`. A no_mangle
function no header declares, like the `ffi_extra.rs` runtime shims, is not ABI surface, and gets no pin.

Where a pin fails, the export is wrong and the header is right. Fix the export's signature and every Rust caller of
it. Typical fixes:
- a C string as `*const u8` or `*const i8` becomes `*const core::ffi::c_char`;
- a status as `i32` becomes `crate::sys::DelugeStatus`;
- a `uint8_t` status as `u32` becomes `u8`.

- [ ] **Step 4: Run both carriers**

Run: `./dbt test bsp-host golden-vt-render unit`
Expected: PASS.

Then run the device link check. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/bsp/rust/src
git commit -m "build: pin every BSP export's signature to its libdeluge declaration"
```

---

### Task 5: Pin the crates' exports, the Rust side's imports of C++ functions, and the copied constants

The in-tree crates (`crates/deluge_*`) export ABI functions but cannot see `sys`. Rust code also calls C++
functions through its own `extern "C"` blocks (spec F1), so a wrong copy there is invisible. And the pure channel
modules copy status constants by hand, because their host tests compile them without `sys`.

**Files:**
- Create: `src/bsp/rust/src/abi_pins.rs`, compiled `#[cfg(any(target_os = "none", feature = "host_app"))]` and
  declared in `main.rs`
- Modify: the crates' ABI modules, so the pinned functions are reachable. `pub` on the item, and a `pub mod abi`
  where the module is private. From the export count: `deluge_sample_stream`, `deluge_audio_format`,
  `deluge_sample_fill`, `deluge_sample_source`, `deluge_alloc`, `deluge_resource`, `deluge_sample_reader`,
  `deluge_rust`.
- Modify: every Rust `extern "C" { … }` block that declares a function a libdeluge header declares. List them:
  `git grep -n -A30 'extern "C" {' -- src/bsp/rust/src crates | grep -o 'fn deluge_[a-z_0-9]*' | sort -u`.

- [ ] **Step 1: Pin the crates' exports**

`src/bsp/rust/src/abi_pins.rs`:

```rust
//! Signature pins for ABI functions defined outside this crate's modules: the in-tree crates' exports, and the
//! C++ functions the Rust side declares for itself. See `abi_pin!` in main.rs.
```

Then add, for each crate, one invocation naming its header-declared exports:

```rust
abi_pin!(in deluge_sample_stream::abi: deluge_sample_stream_define, deluge_sample_stream_release);
```

That line is the pattern. The names come from `grep -h -A2 'no_mangle' crates/<crate>/src/*.rs | grep -o 'fn
[a-zA-Z_0-9]*'`, filtered by `scripts/check_abi_surface.py <names>` to those with a header. A path the macro
cannot reach is a module or item that is not `pub`: make it `pub`.

- [ ] **Step 2: Replace the Rust side's own declarations of C++ functions**

For each function a libdeluge header declares, which the Rust side also declares in an `extern "C"` block:
- in the BSP crate, delete the local declaration, and call `crate::sys::<name>` (or `use crate::sys::<name>;`);
- in a crate, keep the declaration (it cannot see `sys`), make it `pub`, and pin it from `abi_pins.rs` with
  `abi_pin!(in <crate>::<module>: <name>)`.

A function the Rust side calls that no header declares is spec F1's gap. Add its declaration to the header of its
subject, with `DELUGE_NOEXCEPT` and a `[task]`/`[audio]` tag, and then treat it as above. Start from
`deluge_streaming_define_asset` and `deluge_debug_now_frames`, and find the rest with:

```bash
git grep -h -A30 'extern "C" {' -- src/bsp/rust/src crates | grep -o 'fn [a-zA-Z_][a-zA-Z_0-9]*' | cut -d' ' -f2 | sort -u > /tmp/rust_imports.txt
scripts/check_abi_surface.py $(cat /tmp/rust_imports.txt) | awk '$2=="-"{print $1}'
```

Those rows are C symbols Rust calls with no header declaration. Some belong to the C runtime or NE10 (`memcpy`,
`ne10_*`) and are not libdeluge's. Leave those.

- [ ] **Step 3: Pin the copied constants**

In each module that copies a header constant by hand (`file_write.rs`, `file_bytes.rs`, `file_directory.rs`,
`file_prefix.rs`, `audio_loader.rs`, `file_write_slot.rs`, `streaming_loader.rs`, and any other found with
`git grep -n 'pub const DELUGE_' -- src/bsp/rust/src crates`), pin every copy from `abi_pins.rs`:

```rust
abi_const!(
    crate::file_write::DELUGE_FILE_WRITE_PENDING => DELUGE_CHANNEL_PENDING,
    crate::file_write::DELUGE_FILE_WRITE_READY => DELUGE_CHANNEL_READY,
    crate::file_write::DELUGE_FILE_WRITE_UNKNOWN => DELUGE_CHANNEL_UNKNOWN,
    crate::file_write::DELUGE_FILE_WRITE_FAILED => DELUGE_CHANNEL_FAILED,
    crate::file_write_slot::MAX_PATH_CHARS + 1 => DELUGE_PATH_MAX,
);
```

Rewrite each copy's doc comment from "pinned by `static_assert` in `*_abi.cpp`" to "pinned by `abi_pins.rs`".

A copied constant whose module is compiled only with `sys` does not need a copy. Replace it with
`pub use crate::sys::DELUGE_…;` or a typed `const X: u8 = crate::sys::DELUGE_… as u8;`.

Neutering check: change `DELUGE_FILE_WRITE_FAILED`'s copy to 4.
Run: `./dbt test golden-vt-render`
Expected: FAIL, a const-evaluation error naming `DELUGE_CHANNEL_FAILED`. Revert.

- [ ] **Step 4: Run both carriers, and commit**

Run: `./dbt test bsp-host crates golden-vt-render unit`
Expected: PASS.

Then run the device link check. Expected: exit 0.

```bash
git add src/bsp/rust/src crates include/libdeluge
git commit -m "build: pin the crates' exports, Rust's imports of C++ functions, and every copied constant to the headers"
```

---

### Task 6: Delete the `*_abi.cpp` pin files

`src/deluge/storage/file_bytes_abi.cpp`, `file_directory_abi.cpp`, `file_prefix_abi.cpp` and
`file_write_abi.cpp` hold only `static_assert`s. Some pin a constant's value (Task 5 pins those against the
same headers from the Rust side). Some pin one header against another, which `channel.h` made true by
construction. `file_bytes_abi.cpp` also holds P1's `DelugeTimestamp` layout asserts.

**Files:**
- Delete: the four `*_abi.cpp` files
- Modify: the CMake source lists that name them (`git grep -n '_abi.cpp' -- '*.txt' '*.cmake'`)
- Modify: `src/bsp/rust/src/abi_pins.rs` (the timestamp layout, pinned against `sys::DelugeTimestamp`)

- [ ] **Step 1: Move what only the C++ side checks**

Read each file. For every `static_assert` that Task 5 does not already cover through `abi_const!`, add the
equivalent to `abi_pins.rs`.

For P1's timestamp:

```rust
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    use crate::file_bytes::FileTimestamp as Ours;
    use crate::sys::DelugeTimestamp as Theirs;
    assert!(size_of::<Ours>() == size_of::<Theirs>());
    assert!(align_of::<Ours>() == align_of::<Theirs>());
    assert!(offset_of!(Ours, year) == offset_of!(Theirs, year));
    assert!(offset_of!(Ours, month) == offset_of!(Theirs, month));
    assert!(offset_of!(Ours, day) == offset_of!(Theirs, day));
    assert!(offset_of!(Ours, hour) == offset_of!(Theirs, hour));
    assert!(offset_of!(Ours, minute) == offset_of!(Theirs, minute));
    assert!(offset_of!(Ours, second) == offset_of!(Theirs, second));
};
```

The `DelugeStatus` numeric asserts (`DELUGE_ERR_IO == -5` and the like) become
`abi_const!(<the Rust copy> => DelugeStatus_DELUGE_ERR_IO)` against wherever the Rust side copies them (for
example `tests/file_bytes_host.rs`'s `NOT_FOUND`, which lives in a test and is out of reach). If a value is copied
only in a host test, leave it: the test itself fails if the value is wrong.

- [ ] **Step 2: Delete the files and their build entries**

Delete the four files, and remove them from their CMake lists.

Run: `git grep -n '_abi\.cpp' -- ':!docs/superpowers'`
Expected: no mention of these four files. `audio_load_abi.cpp` stays: it holds real code, and P5 moves it.

- [ ] **Step 3: Gates, link, commit**

Run: `./dbt test unit bsp-host golden-vt-render settings-channel`
Expected: PASS.

Then run the device link check. Expected: exit 0.

```bash
git add -A src/deluge/storage src/bsp/rust/src/abi_pins.rs
git commit -m "chore: delete the C++ pin files the Rust-side pins replace"
```

---

### Task 7: Every export is pinned, and stays pinned

**Files:**
- Modify: `scripts/check_abi_surface.py` (a `--pins` mode)
- Modify: `.pre-commit-config.yaml` (run `--check --pins`)

- [ ] **Step 1: Add `--pins`**

In `scripts/check_abi_surface.py`, add:

```python
ABI_PIN = re.compile(r"abi_pin!\s*\(\s*(?:in\s+[\w:]+\s*:)?([^)]*)\)", re.S)


def pinned() -> set[str]:
    """Every function named in an `abi_pin!` invocation, in the BSP crate."""
    names: set[str] = set()
    for path in RUST:
        if "src/bsp/rust/src" not in path.as_posix():
            continue
        for match in ABI_PIN.finditer(strip_comments(path.read_text(errors="replace"))):
            names.update(n.strip() for n in match.group(1).split(",") if n.strip())
    return names
```

In `main()`, add `parser.add_argument("--pins", action="store_true")`. After the loop, compute:

```python
    unpinned = []
    if args.pins:
        pins = pinned()
        unpinned = [n for n in names if scan(n)["rust_def"] and n not in pins]
        for name in unpinned:
            print(f"unpinned: {name}")
    if (args.check and undefined) or unpinned:
        if undefined and args.check:
            print(f"{len(undefined)} declared function(s) with no definition")
        if unpinned:
            print(f"{len(unpinned)} Rust-defined function(s) with no abi_pin!")
        return 1
    return 0
```

Replace the existing `if args.check and undefined:` block with this. Update the docstring's usage list with
`scripts/check_abi_surface.py --pins   # exit 1 if a Rust-defined declared function has no abi_pin!`.

- [ ] **Step 2: Run it, and watch it fail**

Run: `scripts/check_abi_surface.py --check --pins; echo exit=$?`
Expected: `exit=0`. If it lists `unpinned:` names, Task 4 or 5 missed them. Pin them and rerun.

Neutering check: delete `abi_pin!(deluge_boot_finished);` from `boot.rs`.
Run: `scripts/check_abi_surface.py --pins`
Expected: `unpinned: deluge_boot_finished`, exit 1. Restore.

- [ ] **Step 3: Pre-commit, and commit**

In `.pre-commit-config.yaml`:
- change the `check-abi-surface` hook's entry to `python3 scripts/check_abi_surface.py --check --pins`;
- change its `files:` to `^(include/libdeluge/|src/OSLikeStuff/scheduler_api\.h|src/bsp/rust/src/|crates/|scripts/check_abi_surface\.py)`.

```bash
git add scripts/check_abi_surface.py .pre-commit-config.yaml
git commit -m "chore: fail the commit when a libdeluge export has no signature pin"
```

---

### Task 8: The umbrella header and the README

**Files:**
- Modify: `include/libdeluge/libdeluge.h` (include every header)
- Modify: `include/libdeluge/README.md` (rewrite)
- Modify: `src/bsp/rust/wrapper.h` (back to the umbrella alone)

- [ ] **Step 1: The umbrella includes everything**

Replace `libdeluge.h`'s include list with one `#include "X.h"` per header in `include/libdeluge` except itself,
alphabetically, and replace its comment's "scheduler.h is included where…" paragraph with: "Including the umbrella
is always correct. Including one header is a build-time economy, not a boundary."

Replace `wrapper.h`'s list with `#include <libdeluge/libdeluge.h>` alone.

- [ ] **Step 2: Rewrite the README**

`include/libdeluge/README.md`:

````markdown
# `libdeluge`: the platform boundary

These headers are the C ABI between the portable Deluge application (`src/deluge`) and the platform that runs
it. One platform implements them: the Rust BSP (`src/bsp/rust`, on deluge-sdk's `deluge-bsp`), on the device and,
under the `host_app` feature, on the Embassy host that `dbt sim` and the gates run.

## Rules

1. **C, and only C, crosses.** Every declaration is inside `extern "C"`. Only fixed-width integers, `bool`, and
   POD structs and enums with an explicit underlying type cross. No STL, no exceptions, no allocation across the
   boundary, and no unwinding: every function is `DELUGE_NOEXCEPT`.
2. **The header is the contract.** bindgen reads every header (`src/bsp/rust/wrapper.h`), and every Rust export
   is pinned to its generated declaration by `abi_pin!`. A copied constant is pinned by `abi_const!`.
   `scripts/check_abi_surface.py --check --pins` fails when a declared function has no definition or no pin.
   Change a signature in the header, and the build tells you every place that disagrees.
3. **Direction is one-way.** The app includes `<libdeluge/…>`. The platform implements it. `app.h` is the other
   direction: callbacks the app implements and the platform calls. `scripts/check_platform_boundary.py` keeps the
   app from including platform headers.
4. **Call context is documented per function**, as one of:
   - `[isr]`: may be called from interrupt context.
   - `[audio]`: may be called from the audio tier. It must not block, allocate or lock.
   - `[task]`: called from task context only.

## Channels

Storage is reached through request/collect channels: a request returns at once, and a collect is polled once per
tick for the answer (`channel.h`'s `DelugeChannelStatus`). Every channel follows the same rules:
- answers are keyed, and a collect with another key answers UNKNOWN and leaves the answer for its owner;
- answers are stamped with the card generation, so after an ejection an old answer is never collected;
- a request whose buffer the platform may still touch is reported by the channel's `holds()`.

`file_prefix.h`, `file_directory.h`, `file_bytes.h`, `file_write.h`, `audio_load.h` and `streaming_fill.h` are
the channels. Before boot finishes (`deluge::boot::finished()`), the app uses blocking calls instead.

## Host-only declarations

Declarations inside `#ifdef DELUGE_HOST` (the `*_set_latency_ms` knobs, `deluge_trigger_clock_host_edge`) exist
only in the Embassy host build, for the gates. A device image never defines them.
````

- [ ] **Step 3: Gates, link, commit**

Run: `./dbt test unit bsp-host golden-vt-render`
Expected: PASS.

Then run the device link check. Expected: exit 0.

```bash
git add include/libdeluge/libdeluge.h include/libdeluge/README.md src/bsp/rust/wrapper.h
git commit -m "docs: make the libdeluge umbrella complete and describe the ABI's contract"
```

---

### Task 9: Whole-suite check and merge

- [ ] **Step 1:** Run `./dbt test`.
  Expected: every entry PASS, apart from entries whose status is `unavailable`.
- [ ] **Step 2:** Run `scripts/check_abi_surface.py --check --pins --dead`.
  Expected: no dead rows, no undefined names, no unpinned names; exit 0.
- [ ] **Step 3:** Run the device link check.
  Expected: exit 0.
- [ ] **Step 4:** `git switch next && git merge --no-ff chore/libdeluge-header-contract`
