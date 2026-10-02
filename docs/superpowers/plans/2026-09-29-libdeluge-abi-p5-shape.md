# libdeluge ABI P5: the remaining shape — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the ABI's shape:
- the C++-to-C++ half of `audio_load.h` leaves the ABI;
- `streaming_fill.h` holds only the fill queue;
- handles follow one rule, with no fake pointers;
- every path crosses as a bounded, NUL-terminated string;
- every declaration is `noexcept`, reports errors as a `DelugeStatus`, and carries a context tag;
- the scheduler API lives in `include/libdeluge`.

**Architecture:** Each task is a self-contained reshaping, checked by the P3 pins (a signature change that the
Rust side does not follow fails the build), by `scripts/check_abi_surface.py`, and by the gates that drive the
code it touches.

**Tech Stack:** C/C++ headers, C++23, Rust, the P2/P3 surface script.

**Spec:** `docs/superpowers/specs/2026-09-29-libdeluge-abi-overhaul-design.md` (§2 A4, A5, D4, D5, D6, E1–E5, F3)

## Global Constraints

- Branch off `next` after P4 has merged: `git switch -c refactor/libdeluge-shape next`. Local merges go into
  `next`, never `main`. Never push.
- Commit after every task, without asking. No `Co-Authored-By`. Commit subjects name the change, not the process.
- If a pre-commit hook reformats files, re-add them and commit again. Never `--amend`.
- Comments state the current rationale only. New C++ is idiomatic C++23.
- An `#include` folded onto the previous line is silently dropped. Every include you add goes on its own line.
- Don't touch the website (`website/`).
- **Device link check:**
  ```bash
  mv src/bsp/rust/local-sdk.toml /tmp/local-sdk.toml.held 2>/dev/null; ./dbt build RelWithDebInfo; rc=$?; mv /tmp/local-sdk.toml.held src/bsp/rust/local-sdk.toml 2>/dev/null; git checkout -q src/bsp/rust/Cargo.lock; exit $rc
  ```
- `scripts/check_abi_surface.py --check --pins` exits 0 before every commit (the pre-commit hook runs it).

## Review Focus

- **A `std::string_view` that is not NUL-terminated.** It must never reach a C function as `.data()`. Such a view
  is a substring, or a view of a `std::array` filled to the brim. Tested in Task 4:
  `CPath.ASubstringIsTerminatedAtItsEnd`.
- **A path one byte too long.** At `DELUGE_PATH_MAX - 1` characters a path is accepted; at `DELUGE_PATH_MAX` it is
  refused, never truncated. Tested in Task 4: `CPath.TheLongestPathFitsAndOneMoreIsRefused`.
- **Handle 0 after the handle rule.** Handle 0 means "none" everywhere. A stream or file table slot 0 is handle 1
  at the ABI. A handle round-trip that forgets the offset opens the wrong file, or none. Tested in Task 3 by the
  recorder and offset-io gates, and by a unit test of the conversion.
- **A C++ ABI definition that throws.** Adding `noexcept` to a declaration makes a throwing definition call
  `std::terminate`, which on this platform aborts silently. Task 5 greps the C++ definitions of `app.h` callbacks
  for `throw` and for calls into code that throws. The ones in `audio_load_abi.cpp` already catch.
- **A scheduler include that moves.** Twenty-four files include `scheduler_api.h`. Task 6 builds every carrier.

---

### Task 1: The sample-build half of `audio_load.h` becomes C++

The C++ app defines `deluge_audio_load_begin`, `resolve_alternate_dir`, `candidates`, `bind_read_stream`,
`apply_geometry`, `finish` and `abandon` in `audio_load_abi.cpp`, and only C++ calls them. The surface script
shows `rust 0` for all seven. The Rust half, `request`, `collect` and `set_latency_ms`, stays in the ABI.

**Files:**
- Rename: `src/deluge/storage/audio/audio_load_abi.cpp` to `sample_build.cpp`
- Create: `src/deluge/storage/audio/sample_build.h`
- Modify: `include/libdeluge/audio_load.h` (delete the seven declarations, `DelugeSampleBuild`,
  `DelugeAudioLoadGeometry` and every type only they use)
- Modify, the callers: `git grep -ln 'deluge_audio_load_\(begin\|resolve_alternate_dir\|candidates\|bind_read_stream\|apply_geometry\|finish\|abandon\)'`
  (`audio_file_manager.cpp`, the `load_policy` scenario, and any others it lists)

**Interfaces:**
- Produces, in `namespace deluge::storage::sample_build`:
  - `begin`, `resolveAlternateDir`, `candidates`, `bindReadStream`, `applyGeometry`, `finish` and `abandon`, with
    the same parameters as today (C strings may become `std::string_view` if the body copies them anyway);
  - `noexcept` on each;
  - errors as `DelugeStatus` in place of `uint8_t* error_out`, where the value is an `Error` code today. Keep the
    app's `Error` type if that is what the callers use; read them first.

- [ ] **Step 1: Confirm the split**

Run: `scripts/check_abi_surface.py deluge_audio_load_begin deluge_audio_load_resolve_alternate_dir deluge_audio_load_candidates deluge_audio_load_bind_read_stream deluge_audio_load_apply_geometry deluge_audio_load_finish deluge_audio_load_abandon`
Expected: every row reads `rust 0`.

- [ ] **Step 2: Move them**

- Run `git mv` on the `.cpp`.
- Write `sample_build.h` with the seven declarations and their doc comments, moved from `audio_load.h`.
- In the `.cpp`, drop `extern "C"` and put the functions in the namespace. Keep the `try`/`catch` wrappers: they
  are what make `noexcept` honest.
- Move `DelugeSampleBuild` and `DelugeAudioLoadGeometry` into the C++ header as `struct SampleBuild` and
  `struct Geometry`, unless the Rust side names them. Check with `git grep -n 'DelugeSampleBuild\|DelugeAudioLoadGeometry' -- src/bsp/rust crates`;
  a Rust use means that type stays in `audio_load.h`.
- Update every caller to the new names. Update `audio_load.h`'s top comment, which walks through the build
  sequence, to describe only the request/collect half, and to name `storage/audio/sample_build.h` for the build.

- [ ] **Step 3: Run the gates**

Run: `./dbt test unit load-policy audio-load-ab sample-load-tree sample-claim preset-load`
Expected: PASS. LP-ABANDON's 20,000 begin/abandon rounds run through the moved code.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0. Then run the device link check. Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add -A include/libdeluge/audio_load.h src/deluge tests/integration/scenarios
git commit -m "refactor: take the C++-only sample build out of the libdeluge ABI"
```

---

### Task 2: `streaming_fill.h` holds the fill queue, and backing pointers get types

`streaming_fill.h` declares three things:
- **the fill queue:** `signal_fill`, `request_chunk`, `request_drain`, `queue_drained`, `set_fill_context`;
- **streaming-asset lookups:** `deluge_streaming_resource_manager`, `deluge_sample_stream_asset_id` and
  `deluge_streamed_chunk_payload_offset`;
- **three raw efatfs handle calls:** `deluge_efatfs_open`, `close` and `read_at`.

`void*` stands for two different C++ objects: a stream backing and a chunk backing.

**Files:**
- Modify: `include/libdeluge/streaming_fill.h` (keep the fill queue)
- Modify: `include/libdeluge/sample_stream.h` (gains the three asset lookups)
- Modify: `include/libdeluge/file_io.h` (gains the three `deluge_efatfs_*` calls, beside their siblings)
- Modify: `include/libdeluge/types.h`:
  `typedef struct DelugeStreamBacking DelugeStreamBacking;` and
  `typedef struct DelugeChunkBacking DelugeChunkBacking;`
- Modify: every declaration and definition that takes `void* stream_backing` or `void* chunk_backing`. Find them
  with `git grep -n 'void\* *\(stream\|chunk\)_backing\|\*mut c_void.*backing\|\*const c_void.*backing'`.

- [ ] **Step 1: Move the declarations**

Move each group with its doc comments. Add `#include "sample_stream.h"` or `"file_io.h"` to whichever `.cpp` and
`.rs` build step stops seeing a declaration, as the build reports it.

- [ ] **Step 2: Type the backing pointers**

- Replace `void* stream_backing` with `DelugeStreamBacking* stream_backing`, and `void* chunk_backing` with
  `DelugeChunkBacking* chunk_backing`, in the headers.
- On the Rust side, the bindgen types (`*mut sys::DelugeChunkBacking`) are what `abi_pin!` now demands. Update each
  export's parameter to match.
- On the C++ side, each object that crosses gets one conversion pair beside its class, and the other call sites
  use them. The pair is `DelugeChunkBacking* toAbi(Cluster*)` and `Cluster* fromAbi(DelugeChunkBacking*)`, a
  `reinterpret_cast` in one place; the stream backing gets the same with its own class.

- [ ] **Step 3: Run the gates**

Run: `./dbt test unit bsp-host crates golden-vt-render streaming-margin render-song region-fill-differential region-read-differential`
Expected: PASS.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0. Then run the device link check. Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add -A include/libdeluge src crates tests
git commit -m "refactor: split streaming_fill.h by subject and type the backing pointers"
```

---

### Task 3: One handle rule

The two conventions:
- `sample_stream.h` treats 0 as no handle;
- the efatfs file, directory and stream tables hand out slot indices from 0, so `io/file.cpp` and `io/stream.cpp`
  box `handle + 1` into fake `DelugeFile*`/`DelugeDir*`/`DelugeStream*` pointers.

The rule after this task: every ABI handle is a `uint32_t` where 0 is none. The Rust side adds 1 on the way out
and subtracts 1 on the way in. The C++ side stores the number.

**Files:**
- Modify: `include/libdeluge/types.h` (`typedef uint32_t DelugeHandle;` and `#define DELUGE_HANDLE_NONE 0u`, with
  the rule in its doc comment)
- Modify: `include/libdeluge/file_io.h` and `stream_io.h` (handle parameters become `DelugeHandle`, and the opaque
  `DelugeFile`/`DelugeDir`/`DelugeStream` typedefs go)
- Modify: `src/bsp/rust/src/efatfs_fs.rs`, `efatfs_host_shim.rs` and `file_stream.rs`: the ±1 at each export,
  in two helpers, `fn to_abi(slot: u32) -> u32 { slot + 1 }` and
  `fn from_abi(handle: u32) -> Option<u32> { handle.checked_sub(1) }`. A handle of 0 is refused as
  `DELUGE_ERR_PARAM`, or as a refused request.
- Modify: `src/deluge/io/file.cpp`/`file.hpp`, `stream.cpp`/`stream.hpp` (delete `box_*`/`unbox_*`, and store
  `DelugeHandle handle_ = DELUGE_HANDLE_NONE`)
- Test: `src/bsp/rust/tests/handle_host.rs` (the two helpers)

- [ ] **Step 1: Write the failing test**

Put the helpers in a pure module, `src/bsp/rust/src/abi_handle.rs`, and test it.
`src/bsp/rust/tests/handle_host.rs`:

```rust
#![cfg(not(target_os = "none"))]

#[path = "../src/abi_handle.rs"]
mod abi_handle;

use abi_handle::{from_abi, to_abi};

#[test]
fn slot_zero_is_handle_one() {
    assert_eq!(to_abi(0), 1);
    assert_eq!(from_abi(1), Some(0));
}

#[test]
fn handle_zero_is_none() {
    assert_eq!(from_abi(0), None);
}

#[test]
fn every_slot_round_trips() {
    for slot in 0..64 {
        assert_eq!(from_abi(to_abi(slot)), Some(slot));
    }
}
```

Run: `./dbt test bsp-host`
Expected: FAIL: `src/abi_handle.rs` is not found.

- [ ] **Step 2: Write the helpers**

`src/bsp/rust/src/abi_handle.rs`:

```rust
//! The ABI's handle rule: a handle is a `u32`, and 0 is none (`DELUGE_HANDLE_NONE`). The tables behind the handles
//! index their slots from 0, so a slot crosses the ABI as `slot + 1`.

/// The handle for table slot `slot`.
pub fn to_abi(slot: u32) -> u32 {
    slot + 1
}

/// The table slot for `handle`, or `None` for `DELUGE_HANDLE_NONE`.
pub fn from_abi(handle: u32) -> Option<u32> {
    handle.checked_sub(1)
}
```

Declare `mod abi_handle;` in `main.rs`, unconditionally.

Run: `./dbt test bsp-host`
Expected: PASS.

- [ ] **Step 3: Apply the rule at every handle export**

At each `deluge_efatfs_*` and `deluge_file_stream_*` export that returns or takes a table handle, convert with the
helpers. A `from_abi` of `None` fails the call as it fails an out-of-range handle today. Change the header
parameter types to `DelugeHandle`, delete the three opaque typedefs, and replace `box_*`/`unbox_*` in the C++ with
the stored number.

Check nothing still boxes:

Run: `git grep -n 'box_file_handle\|box_dir_handle\|box_stream_handle\|DelugeFile\b\|DelugeDir\b\|DelugeStream\b' -- src include tests ':!docs/superpowers'`
Expected: no output, apart from `DelugeStreamMode`, `DelugeStreamBacking` and other longer names, which do not
match `\b`-bounded.

- [ ] **Step 4: Run the gates**

Run: `./dbt test unit bsp-host golden-vt-render recorder-write offset-io path-operations save-roundtrip song-load-channel fs-differential`
Expected: PASS.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0. Then run the device link check. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add -A include/libdeluge src/bsp/rust src/deluge/io tests
git commit -m "refactor: give every ABI handle one rule, with 0 as none, and drop the pointer boxing"
```

---

### Task 4: `deluge::io::CPath`

C++ callers pass `std::string_view::data()` to C functions that read up to a NUL. For example, `io/file.cpp:144`
and `:198` pass the `path` parameter's `.data()`. A view of a substring or of a full array has no NUL, and the
callee reads past it.

**Files:**
- Create: `src/deluge/io/c_path.h`
- Create: `tests/unit/cpputest/c_path_tests.cpp`
- Modify: every C++ call that passes a `std::string_view`'s `.data()` as a C string to a `deluge_*` function. Find
  them with `git grep -n 'deluge_[a-z_]*(.*\.data()' -- src/deluge`, and read each: a `.data()` of a byte buffer
  is not a path and stays.

**Interfaces:**
- Produces: `deluge::io::CPath`, with:
  - `static std::optional<CPath> from(std::string_view) noexcept`, which is empty when the path is too long or
    contains a NUL;
  - `const char* c_str() const noexcept`;
  - `std::string_view view() const noexcept`.

- [ ] **Step 1: Write the failing test**

`tests/unit/cpputest/c_path_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "io/c_path.h"

#include <cstring>
#include <string>

using deluge::io::CPath;

TEST_GROUP(CPath){};

TEST(CPath, ASubstringIsTerminatedAtItsEnd) {
	const std::string whole = "SONGS/SONG001.XML trailing";
	auto path = CPath::from(std::string_view{whole}.substr(0, 17));
	CHECK_TRUE(path.has_value());
	STRCMP_EQUAL("SONGS/SONG001.XML", path->c_str());
}

TEST(CPath, TheLongestPathFitsAndOneMoreIsRefused) {
	const std::string longest(DELUGE_PATH_MAX - 1, 'a');
	CHECK_TRUE(CPath::from(longest).has_value());
	CHECK_EQUAL(DELUGE_PATH_MAX - 1, std::strlen(CPath::from(longest)->c_str()));
	const std::string tooLong(DELUGE_PATH_MAX, 'a');
	CHECK_FALSE(CPath::from(tooLong).has_value());
}

TEST(CPath, AnEmbeddedNulIsRefused) {
	const std::string withNul("A\0B", 3);
	CHECK_FALSE(CPath::from(withNul).has_value());
}

TEST(CPath, TheViewIsThePath) {
	auto path = CPath::from("KITS/KIT000.XML");
	CHECK_TRUE(path->view() == "KITS/KIT000.XML");
}
```

Run: `./dbt test unit`
Expected: FAIL: `io/c_path.h: No such file or directory`.

- [ ] **Step 2: Write it**

`src/deluge/io/c_path.h`:

```cpp
#pragma once

#include <libdeluge/channel.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace deluge::io {

/// A path as a C function takes it: NUL-terminated, and at most `DELUGE_PATH_MAX` bytes with the NUL.
///
/// A `std::string_view` carries no terminator, so its `data()` must never go to a function that reads up to one.
/// Make a CPath from it instead. A path that does not fit is refused, never truncated: a truncated path names
/// another file, which may exist.
class CPath {
public:
	[[nodiscard]] static std::optional<CPath> from(std::string_view path) noexcept {
		if (path.size() >= DELUGE_PATH_MAX || path.find('\0') != std::string_view::npos) {
			return std::nullopt;
		}
		CPath out;
		std::ranges::copy(path, out.bytes_.begin());
		out.bytes_[path.size()] = '\0';
		out.size_ = path.size();
		return out;
	}

	[[nodiscard]] const char* c_str() const noexcept { return bytes_.data(); }
	[[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }

private:
	CPath() = default;
	std::array<char, DELUGE_PATH_MAX> bytes_{};
	std::size_t size_ = 0;
};

} // namespace deluge::io
```

Run: `./dbt test unit`
Expected: PASS.

- [ ] **Step 3: Use it at every such call**

At each call found under Files:

```cpp
	const auto cpath = CPath::from(path);
	if (!cpath) {
		return /* the function's existing "bad argument" answer, e.g. */ std::unexpected(Error::FILE_NOT_FOUND);
	}
	DelugeStatus st = deluge_efatfs_file_open(cpath->c_str(), static_cast<uint8_t>(mode), &handle);
```

The return in the refusal branch is each function's own existing failure for a bad path: read the function and
use what it already returns when the C call fails with `DELUGE_ERR_PARAM`. The `/* … */` in the snippet shows the
shape and is not text to copy.

Run: `git grep -n 'deluge_[a-z_]*([^)]*path\.data()' -- src/deluge`
Expected: no output.

- [ ] **Step 4: Run the gates and commit**

Run: `./dbt test unit path-operations offset-io save-roundtrip song-load-channel preset-load sample-claim`
Expected: PASS.

```bash
git add src/deluge/io/c_path.h tests/unit/cpputest/c_path_tests.cpp src/deluge
git commit -m "fix: terminate every path the app hands a C function"
```

---

### Task 5: `noexcept`, `DelugeStatus` and the enum spelling everywhere

**Files:**
- Modify: every header without `DELUGE_NOEXCEPT` on its declarations. After P3, all of them include `abi.h`.
  Find them with `grep -L DELUGE_NOEXCEPT include/libdeluge/*.h`.
- Modify: the C++ definitions of `app.h` callbacks and of any other C++-defined ABI function (the surface script's
  `def` column, where the definition is in `src/deluge`)
- Modify: the ABI functions that report failure some other way (the list comes from Step 2)
- Modify: `include/libdeluge/tier.h` (the anonymous `enum` becomes `typedef enum DelugeTier : uint8_t`, and
  `deluge_tier_current` returns `DelugeTier`)

- [ ] **Step 1: `noexcept` on every declaration**

Append `DELUGE_NOEXCEPT` before the `;` of every function declaration in the listed headers.

Run: `./dbt test unit bsp-host golden-vt-render`

A C++ definition whose declaration is now `noexcept` and whose definition is not fails to compile ("exception
specification … does not match"). Add `noexcept` to each such definition. Before adding it, read the body: a body
that can throw (it allocates with `new`, or calls code that throws) gets a `try { … } catch (...) { … }` that
returns the function's failure value, as the `audio_load` definitions already do. `std::terminate` on this
platform aborts silently.

Run: `grep -nE '\)\s*;\s*$' include/libdeluge/*.h | grep -v 'DELUGE_NOEXCEPT\|typedef\|(\*'`
Expected: no output. Every function declaration ends `) DELUGE_NOEXCEPT;`, and the only other lines ending `);`
are function-pointer typedefs.

- [ ] **Step 2: Errors as `DelugeStatus`**

List the ABI functions that report failure another way:

```bash
grep -n -E '(int|int32_t|uint8_t)\s*\*\s*(error|err|status)[a-z_]*\s*[,)]|^int(32_t)?\s+deluge_' include/libdeluge/*.h
```

For each hit:
- its error out-parameter becomes `DelugeStatus*`, or its `int` return becomes `DelugeStatus`;
- the Rust definition follows, and `abi_pin!` fails until it does;
- the C++ caller compares against `DELUGE_OK`/`DELUGE_ERR_*`.

A `bool` return that means "refused, ask again" (a channel request) stays a `bool`: it is not an error.

- [ ] **Step 3: The tier enum**

In `tier.h`:

```c
/// The tiers code runs on.
typedef enum DelugeTier : uint8_t {
	DELUGE_TIER_INTERACTION = 0, ///< thread mode: the UI, the sequencer, the app's tasks
	DELUGE_TIER_STORAGE = 1,     ///< the storage tasks
	DELUGE_TIER_AUDIO = 2,       ///< the audio render
} DelugeTier;

/// @brief The tier running the caller. [task] [audio] [isr]
DelugeTier deluge_tier_current(void) DELUGE_NOEXCEPT;
```

Delete "A C++-only build answers…" and "Zero in a C++-only build." from `tier.h`'s docs. `tier.rs`'s export
returns `sys::DelugeTier`.

Run: `grep -n '^enum\|^typedef enum [A-Za-z]* {' include/libdeluge/*.h`
Expected: no output. Every enum has an explicit underlying type.

- [ ] **Step 4: Run the gates and commit**

Run: `./dbt test`
Expected: every entry PASS, apart from entries whose status is `unavailable`. This task touches every header.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0. Then run the device link check. Expected: exit 0.

```bash
git add -A include/libdeluge src tests crates
git commit -m "refactor: make every ABI function noexcept, report failures as DelugeStatus, and give every enum a width"
```

---

### Task 6: The scheduler API moves into `include/libdeluge`

`include/libdeluge/scheduler.h` only includes `src/OSLikeStuff/scheduler_api.h`, which includes
`task_scheduler/clock_type.h`.

**Files:**
- Move: the contents of `src/OSLikeStuff/scheduler_api.h` into `include/libdeluge/scheduler.h`
- Move: `dTime` and `DELUGE_CLOCKS_PER*`, the C half of `clock_type.h`, into `include/libdeluge/scheduler.h`.
  The C++ half (`Time`, `rollTime`) stays in `clock_type.h`, which includes `<libdeluge/scheduler.h>` for them.
- Delete: `src/OSLikeStuff/scheduler_api.h`
- Modify: the 24 files that include it (`git grep -l 'scheduler_api.h'`): `#include <libdeluge/scheduler.h>`
- Modify: `scripts/check_abi_surface.py` (drop the `src/OSLikeStuff/scheduler_api.h` entry from `HEADERS`)
- Modify: `src/bsp/rust/build.rs` (drop the `-I src` clang argument P3 added, if nothing else needs it)

- [ ] **Step 1: Move, and repoint the includes**

Keep every declaration and doc comment as it is, apart from adding `DELUGE_NOEXCEPT` (Task 5's rule) and a
context tag (Task 7's rule). Includes stay outside the `extern "C"` block, as the current file's comment explains.

```bash
git grep -l 'scheduler_api.h' -- ':!docs/superpowers' | xargs sed -i -e 's|#include <OSLikeStuff/scheduler_api.h>|#include <libdeluge/scheduler.h>|' -e 's|#include "OSLikeStuff/scheduler_api.h"|#include <libdeluge/scheduler.h>|' -e 's|#include "scheduler_api.h"|#include <libdeluge/scheduler.h>|'
git rm src/OSLikeStuff/scheduler_api.h
```

Run: `git grep -n 'scheduler_api' -- ':!docs/superpowers'`
Expected: no output.

- [ ] **Step 2: Run the gates and commit**

Run: `./dbt test unit bsp-host golden-vt-render`
Expected: PASS. Then run the device link check. Expected: exit 0.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0, with the scheduler functions listed under `scheduler.h`.

```bash
git add -A include/libdeluge src scripts/check_abi_surface.py src/bsp/rust/build.rs
git commit -m "refactor: move the scheduler API into the libdeluge headers"
```

---

### Task 7: Every declaration says where it may be called from

**Files:**
- Modify: `scripts/check_abi_surface.py` (a `--tags` mode)
- Modify: every header whose declarations lack a tag
- Modify: `.pre-commit-config.yaml` (add `--tags` to the hook)

- [ ] **Step 1: Add `--tags`, and watch it fail**

In `scripts/check_abi_surface.py`, add:

```python
TAG = re.compile(r"\[(task|audio|isr)\]")


def untagged() -> list[str]:
    """Declared functions whose doc comment (the comment lines directly above the declaration) has no context tag."""
    missing = []
    for header in HEADERS:
        lines = header.read_text().splitlines()
        text = "\n".join(lines)
        for match in DECLARATION.finditer(strip_comments(text)):
            words = match.group(1).split()
            if not words or words[0] in NOT_A_RETURN_TYPE:
                continue
            name = match.group(2)
            index = next((i for i, line in enumerate(lines) if re.search(r"\b" + re.escape(name) + r"\s*\(", line)), None)
            if index is None:
                continue
            doc = []
            i = index - 1
            while i >= 0 and lines[i].lstrip().startswith(("///", "//", "*", "/*")):
                doc.append(lines[i])
                i -= 1
            if not TAG.search("\n".join(doc)) and not TAG.search(lines[index]):
                missing.append(name)
    return missing
```

In `main()`, add `parser.add_argument("--tags", action="store_true")`. When it is set, print
`untagged: <name>` for each name from `untagged()`, and include them in the exit-1 condition beside
`undefined` and `unpinned`.

Run: `scripts/check_abi_surface.py --tags; echo exit=$?`
Expected: `exit=1`, with a list of untagged functions. That list is Step 2's work.

- [ ] **Step 2: Tag them**

Tag each listed function in its doc comment:
- `[audio]` if the audio render calls it. These are the functions of `audio_io.h`; `sample_reader.h`,
  `sample_source.h` and `sample_stream.h` (the voices read samples on the audio tier); `streaming_fill.h`'s
  `signal_fill` and `request_chunk`; and `tier.h`'s `deluge_tier_current`.
- `[isr]` if an interrupt handler may call it. That is `deluge_log`, `fault.h`, and any function whose doc
  already says interrupt.
- `[task]` for everything else.

A function that is more than one of these gets every tag that applies. Where a function's callers are unclear,
find them with `git grep -n '<name>(' -- src/deluge` and tag by the tier of the caller. A caller inside
`AudioEngine::routine` or a voice's render is `[audio]`.

Run: `scripts/check_abi_surface.py --check --pins --tags; echo exit=$?`
Expected: `exit=0`.

- [ ] **Step 3: Turn it on, and commit**

Change the hook's entry to `python3 scripts/check_abi_surface.py --check --pins --tags`.

Neutering check: delete one tag, and run `pre-commit run check-abi-surface --all-files`.
Expected: FAIL, naming that function. Restore the tag.

```bash
git add scripts/check_abi_surface.py .pre-commit-config.yaml include/libdeluge
git commit -m "docs: tag every libdeluge function with the contexts that may call it"
```

---

### Task 8: Whole-suite check and merge

- [ ] **Step 1:** Run `./dbt test`.
  Expected: every entry PASS, apart from entries whose status is `unavailable`.
- [ ] **Step 2:** Run `scripts/check_abi_surface.py --check --pins --tags --dead`.
  Expected: exit 0, and no dead rows.
- [ ] **Step 3:** Run the device link check.
  Expected: exit 0.
- [ ] **Step 4:** `git switch next && git merge --no-ff refactor/libdeluge-shape`
