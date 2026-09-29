# libdeluge ABI P4: one channel shape — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:**
- The single-slot channels share one generic, tested answer slot in Rust.
- The C++ callers share one stall bound.
- `file_write` becomes `file_stream` (handle operations only).
- Whole-file reads and writes join the path-keyed byte channel, where a failure says why.

**Architecture:**
- `crates/deluge_channel` holds `AnswerSlot<K, V>`. `file_directory`, `file_prefix`, `file_bytes` and the write
  channel's P1 slot each keep their module's functions and become thin wrappers over it. Their existing host tests
  are the evidence that behaviour did not change.
- `card.rs` flushes from one table.
- `deluge::io::StallBound` replaces twelve hand-rolled 600-tick counters.
- The byte channel gains caller-buffer read and write operations (`read_into`, `write_from`) and `holds()`. Its
  answers carry a `DelugeStatus`.
- The stream channel is renamed. Its open is keyed by path, and it reports why an open failed.

**Tech Stack:** Rust (no_std crate, Embassy BSP), C++23, CppUTest, the scenario gates.

**Spec:** `docs/superpowers/specs/2026-09-29-libdeluge-abi-overhaul-design.md` (§2 B1, B3, B4, B5, B6, F4; §3
decisions 3, 7 and 8)

**Scope:** `audio_load` and `streaming_fill` keep their own structures. They are handle tables with many entries
in flight, not single-flight slots, and the P1–P3 work already gave them the shared vocabulary and pins.

## Global Constraints

- Branch off `next` after P3 has merged: `git switch -c refactor/libdeluge-channels next`. Local merges go into
  `next`, never `main`. Never push.
- Commit after every task, without asking. No `Co-Authored-By`. Commit subjects name the change, not the process.
- If a pre-commit hook reformats files, re-add them and commit again. Never `--amend`.
- Comments state the current rationale only. New C++ is idiomatic C++23, and every tick count says it counts
  caller ticks.
- Don't touch the website (`website/`).
- **Device link check:**
  ```bash
  mv src/bsp/rust/local-sdk.toml /tmp/local-sdk.toml.held 2>/dev/null; ./dbt build RelWithDebInfo; rc=$?; mv /tmp/local-sdk.toml.held src/bsp/rust/local-sdk.toml 2>/dev/null; git checkout -q src/bsp/rust/Cargo.lock; exit $rc
  ```
- **The pins from P3 stay green.** Every new or renamed export gets an `abi_pin!`, and
  `scripts/check_abi_surface.py --check --pins` exits 0 before each commit. The pre-commit hook runs it.
- A port task changes no behaviour. If a host test must change to pass, stop: the port is wrong.

## Review Focus

- **A key compared by a different rule after the port.** `heapless::String` against `&str` compares contents;
  an `Option` key or a path with a trailing `/` must compare exactly as before. The unchanged host tests of each
  ported module hold this. Owner: Tasks 2 and 3, which must not edit those tests.
- **A write refused while the byte channel is busy.** The byte channel's existing `request_read` is
  fire-and-forget, but `write_from` must refuse, never replace, when a request is queued: a replaced document write
  loses a save. Tested in Task 5: `a_second_write_from_is_refused_not_replaced`.
- **A buffer freed while the byte channel still holds it.** `deluge_file_bytes_holds` must say true from request to
  terminal collect, as `deluge_file_write_holds` does. `staging-release` covers it once staging moves in Task 5.
- **"The name was taken" versus a card error.** A `write_from` onto an existing name with `may_overwrite = false`
  must answer `DELUGE_ERR_EXISTS`, not `DELUGE_ERR_IO`, and must leave the existing file untouched.
  `document-save-channel`'s DS-OCCUPIED holds both.
- **The recorder's folder retry.** It retries on NOT_FOUND only. With `collect_open` reporting a status, a card
  error must no longer look like a missing folder. Tested in Task 6 by the recorder gate plus a slot test.

---

### Task 1: `crates/deluge_channel`

**Files:**
- Create: `crates/deluge_channel/Cargo.toml`, `crates/deluge_channel/src/lib.rs`,
  `crates/deluge_channel/tests/slot.rs`
- Modify: `crates/Cargo.toml` (add `"deluge_channel"` to `members`)
- Modify: `src/bsp/rust/Cargo.toml` and `src/bsp/rust/harness/golden_vt_render/Cargo.toml` (the path dependency)

**Interfaces:**
- Produces:
  - `deluge_channel::Answer`: a trait with `fn orphan(&self) -> Option<u32> { None }`.
  - `deluge_channel::Take<V>`: `Ready(V)` or `Absent { orphan: Option<u32> }`.
  - `deluge_channel::AnswerSlot<K, V>`, with these methods:
    - `const fn new() -> Self`
    - `fn is_empty(&self) -> bool`
    - `fn key(&self) -> Option<&K>`
    - `fn publish(&mut self, key: K, value: V, generation: u32) -> Option<u32>`
    - `fn take<Q: ?Sized>(&mut self, key: &Q, generation: u32) -> Take<V> where K: PartialEq<Q>`
    - `fn discard<Q: ?Sized>(&mut self, key: &Q) -> Option<u32> where K: PartialEq<Q>`
    - `fn drain(&mut self) -> Option<u32>`
  - The rules:
    - a take with another key leaves the answer and returns `Absent { orphan: None }`;
    - a take with this key and another generation removes the answer and returns `Absent` with its orphan;
    - `publish`, `discard` and `drain` return the orphan of whatever they drop.

- [ ] **Step 1: Write the failing tests**

`crates/deluge_channel/tests/slot.rs`:

```rust
use deluge_channel::{Answer, AnswerSlot, Take};

/// An answer that may carry a handle, as an open does.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Outcome {
    Opened(u32),
    Failed,
}

impl Answer for Outcome {
    fn orphan(&self) -> Option<u32> {
        match self {
            Outcome::Opened(h) => Some(*h),
            Outcome::Failed => None,
        }
    }
}

/// Nonzero: a publish that dropped its stamp would leave 0.
const CARD: u32 = 3;

fn slot() -> AnswerSlot<String, Outcome> {
    AnswerSlot::new()
}

#[test]
fn an_answer_is_taken_once() {
    let mut s = slot();
    assert_eq!(s.publish("A".into(), Outcome::Opened(1), CARD), None);
    assert_eq!(s.take("A", CARD), Take::Ready(Outcome::Opened(1)));
    assert_eq!(s.take("A", CARD), Take::Absent { orphan: None });
    assert!(s.is_empty());
}

#[test]
fn another_key_leaves_the_answer() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(1), CARD);
    assert_eq!(s.take("B", CARD), Take::Absent { orphan: None });
    assert_eq!(s.key().map(String::as_str), Some("A"));
    assert_eq!(s.take("A", CARD), Take::Ready(Outcome::Opened(1)));
}

#[test]
fn a_stale_answer_is_removed_and_its_handle_orphaned() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(7), CARD);
    assert_eq!(s.take("A", CARD + 1), Take::Absent { orphan: Some(7) });
    assert!(s.is_empty());
}

#[test]
fn a_stale_failure_orphans_nothing() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Failed, CARD);
    assert_eq!(s.take("A", CARD + 1), Take::Absent { orphan: None });
    assert!(s.is_empty());
}

#[test]
fn publishing_over_an_answer_orphans_it() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(2), CARD);
    assert_eq!(s.publish("B".into(), Outcome::Failed, CARD), Some(2));
    assert_eq!(s.take("B", CARD), Take::Ready(Outcome::Failed));
}

#[test]
fn discard_drops_only_its_own_key() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(4), CARD);
    assert_eq!(s.discard("B"), None);
    assert!(!s.is_empty());
    assert_eq!(s.discard("A"), Some(4));
    assert!(s.is_empty());
}

#[test]
fn drain_empties_and_orphans() {
    let mut s = slot();
    s.publish("A".into(), Outcome::Opened(5), CARD);
    assert_eq!(s.drain(), Some(5));
    assert!(s.is_empty());
    assert_eq!(s.drain(), None);
}
```

`crates/deluge_channel/Cargo.toml`:

```toml
# deluge_channel: the answer slot every single-flight storage channel keeps.
#
# A crate rather than a BSP module for the reason `deluge_card_generation` is one: the channel modules are also
# `#[path]`-included into their own host tests, where `crate::` names the test binary. An extern-crate path
# resolves the same in both.
[package]
name = "deluge_channel"
version = "0.1.0"
edition = "2021"
license = "GPL-3.0-or-later"
publish = false

[lib]
name = "deluge_channel"
crate-type = ["rlib"]

[dependencies]
```

Add `"deluge_channel"` to `members` in `crates/Cargo.toml`, keeping the list alphabetical. Leave `src/lib.rs`
empty for now (`#![no_std]` alone).

- [ ] **Step 2: Run to verify it fails**

Run: `./dbt test crates`
Expected: FAIL. `tests/slot.rs` does not compile: `Answer`, `AnswerSlot` and `Take` are not found.

- [ ] **Step 3: Write the slot**

`crates/deluge_channel/src/lib.rs`:

```rust
//! The answer slot every single-flight storage channel keeps.
//!
//! A channel takes one request at a time and publishes one answer, keyed by what was asked and stamped with the
//! card generation it was asked under. The rules:
//! - A take with another key leaves the answer: it belongs to an operation that may be about to collect it.
//! - A take with this key but another generation removes the answer and reports nothing: it is from a card that
//!   has gone, and leaving it resident would let a later take find it.
//! - An answer that carries a resource (an open handle) and leaves the slot uncollected is an orphan. Every
//!   method that drops an answer returns its orphan, for the channel to release.
#![no_std]

/// What a channel publishes.
pub trait Answer {
    /// A handle this answer carries, which must be closed if nobody collects the answer.
    fn orphan(&self) -> Option<u32> {
        None
    }
}

/// What a take found.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Take<V> {
    /// This key's answer, for this card.
    Ready(V),
    /// Nothing for this key and card. `orphan` is the handle of a stale answer this take removed.
    Absent { orphan: Option<u32> },
}

struct Entry<K, V> {
    key: K,
    value: V,
    generation: u32,
}

/// One answer, or none.
pub struct AnswerSlot<K, V> {
    entry: Option<Entry<K, V>>,
}

impl<K, V: Answer> AnswerSlot<K, V> {
    pub const fn new() -> Self {
        Self { entry: None }
    }

    pub fn is_empty(&self) -> bool {
        self.entry.is_none()
    }

    /// The key of the answer held, if any.
    pub fn key(&self) -> Option<&K> {
        self.entry.as_ref().map(|e| &e.key)
    }

    /// Hold `key`'s answer, stamped `generation`. Returns the orphan of the answer it replaces.
    pub fn publish(&mut self, key: K, value: V, generation: u32) -> Option<u32> {
        let orphan = self.drain();
        self.entry = Some(Entry {
            key,
            value,
            generation,
        });
        orphan
    }

    /// Take `key`'s answer if it is stamped `generation`.
    pub fn take<Q: ?Sized>(&mut self, key: &Q, generation: u32) -> Take<V>
    where
        K: PartialEq<Q>,
    {
        if !self.entry.as_ref().is_some_and(|e| e.key == *key) {
            return Take::Absent { orphan: None };
        }
        let entry = self.entry.take().expect("checked just above");
        if entry.generation == generation {
            Take::Ready(entry.value)
        } else {
            Take::Absent {
                orphan: entry.value.orphan(),
            }
        }
    }

    /// Drop the answer if it is `key`'s. Returns its orphan.
    pub fn discard<Q: ?Sized>(&mut self, key: &Q) -> Option<u32>
    where
        K: PartialEq<Q>,
    {
        if self.entry.as_ref().is_some_and(|e| e.key == *key) {
            self.drain()
        } else {
            None
        }
    }

    /// Empty the slot. Returns the orphan of what it held.
    pub fn drain(&mut self) -> Option<u32> {
        self.entry.take().and_then(|e| e.value.orphan())
    }
}
```

`String: PartialEq<str>` makes `take("A", …)` compile in the tests. `heapless::String<N>` implements
`PartialEq<str>` as well, which is what the ports rely on.

- [ ] **Step 4: Run to verify it passes**

Run: `./dbt test crates`
Expected: PASS, with the seven `slot` tests listed.

- [ ] **Step 5: Depend on it, and commit**

Add to `src/bsp/rust/Cargo.toml`, beside `deluge_card_generation`:
`deluge_channel = { path = "../../../crates/deluge_channel" }`. Add the same to `golden_vt_render/Cargo.toml`,
with its longer relative path (`../../../../../crates/deluge_channel`).

Run: `./dbt test bsp-host golden-vt-render`
Expected: PASS, since nothing uses it yet.

```bash
git add crates/Cargo.toml crates/deluge_channel src/bsp/rust/Cargo.toml src/bsp/rust/harness/golden_vt_render/Cargo.toml
git commit -m "feat: add the storage channels' shared answer slot"
```

---

### Task 2: `file_directory` and `file_prefix` on the shared slot

**Files:**
- Modify: `src/bsp/rust/src/file_directory.rs` (`DirectorySlot`, `publish_into`, `take_from`, `drain`, lines
  134-245 today)
- Modify: `src/bsp/rust/src/file_prefix.rs` (`PrefixSlot`, `publish_into`, `take_from`, lines 138-300 today)
- Test (unchanged): `src/bsp/rust/tests/file_directory_host.rs`, `file_prefix_host.rs`

**Interfaces:**
- Consumes: `deluge_channel::{Answer, AnswerSlot, Take}`.
- Produces: the same public names and signatures each module has today.

- [ ] **Step 1: Record the tests as they are**

Run: `./dbt test bsp-host 2>&1 | grep -E 'file_(directory|prefix)_host|test result'`
Expected: both suites pass. Note the test counts. After the port they must pass with the same counts, and neither
file may be edited.

- [ ] **Step 2: Port `file_directory`**

- Make `DirOutcome` implement `deluge_channel::Answer`, with `fn orphan(&self) -> Option<u32> { self.handle() }`:
  the handle an `Opened` outcome carries is exactly what `drain` releases today.
- Replace `DirEntry` and `DirectorySlot`'s field with `inner: deluge_channel::AnswerSlot<DirPath, DirOutcome>`.
  `new()` becomes `Self { inner: AnswerSlot::new() }`, and `is_empty()` forwards.
- Rewrite the three functions:

```rust
pub fn take_from(slot: &mut DirectorySlot, path: &str, generation: u32) -> DirTake {
    match slot.inner.take(path, generation) {
        deluge_channel::Take::Ready(outcome) => DirTake::Ready(outcome),
        deluge_channel::Take::Absent { orphan } => DirTake::Absent { orphan },
    }
}

pub fn drain(slot: &mut DirectorySlot) -> Option<u32> {
    slot.inner.drain()
}
```

`publish_into` keeps its signature and turns `key: &str` into a `DirPath` exactly as it does today (including its
over-long-key handling). It then calls `slot.inner.publish(path, outcome, generation)`, and returns or discards
the orphan the way the current body treats a displaced entry. Read the current body before replacing it. If it
closes nothing on displacement today, return what the caller ignores, and do not add behaviour.

Delete the doc paragraphs that restate the slot rules, and point to `deluge_channel` instead. Keep the paragraphs
that are about directories.

- [ ] **Step 3: Port `file_prefix`**

`PrefixEntry` holds a length (or `None` for a failed read) beside the key and stamp. The bytes live in the module's
`store`, outside the slot. Make the slot value that length:
- `impl deluge_channel::Answer for PrefixLen {}` on a new `#[derive(Clone, Copy, PartialEq, Eq, Debug)] pub struct
  PrefixLen(pub Option<usize>);`, since a prefix carries no handle;
- `PrefixSlot` holds `inner: AnswerSlot<PrefixPath, PrefixLen>`;
- `publish_into` writes the bytes into `store` as it does now, then publishes the length;
- `take_from` takes the length through `inner.take(path, generation)`, then copies out of `store` exactly as it
  does now.

- [ ] **Step 4: Run to verify nothing changed**

Run: `./dbt test bsp-host song-preview-ab`
Expected: PASS, with the same `file_directory_host` and `file_prefix_host` test counts as Step 1.

Run: `git diff --stat src/bsp/rust/tests`
Expected: no output.

Then run the device link check. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/bsp/rust/src/file_directory.rs src/bsp/rust/src/file_prefix.rs
git commit -m "refactor: put the directory and prefix channels on the shared answer slot"
```

---

### Task 3: `file_bytes` and the stream channel's slot on the shared slot; one flush table

**Files:**
- Modify: `src/bsp/rust/src/file_bytes.rs` (`BytesSlot`, `publish_into`, `take_from`, `drain`, lines 242-373
  today)
- Modify: `src/bsp/rust/src/file_write_slot.rs` (P1's slot: keep its orphan list, and hold the answer in
  `AnswerSlot<OpKey, Result<u32, u32>>`-shaped storage)
- Modify: `src/bsp/rust/src/card.rs` (one table of flushes)
- Test (unchanged): `src/bsp/rust/tests/file_bytes_host.rs`, `file_write_host.rs`

**Interfaces:**
- Produces: the same public names and signatures. `file_write_slot::AnswerSlot` keeps its methods (`publish`,
  `take`, `discard`, `drain`, `take_orphan`, `has_orphans`).
- Produces: `card::FLUSHES: [fn(); N]`.

- [ ] **Step 1: Record the tests as they are**

Run: `./dbt test bsp-host 2>&1 | grep -E 'file_(bytes|write)_host|test result'`
Note the counts.

- [ ] **Step 2: Port `file_bytes`**

`impl deluge_channel::Answer for ByteOutcome {}`: no byte outcome carries a handle. The key is `(ByteOp, path)`:

```rust
/// What a byte-channel answer is keyed by: the operation and its path.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct ByteKey {
    pub op: ByteOp,
    pub path: BytesPath,
}

impl PartialEq<(ByteOp, &str)> for ByteKey {
    fn eq(&self, other: &(ByteOp, &str)) -> bool {
        self.op == other.0 && self.path.as_str() == other.1
    }
}
```

`BytesSlot` holds `inner: AnswerSlot<ByteKey, ByteOutcome>`. The functions become:
- `take_from(slot, op, path, generation)`, which maps `inner.take(&(op, path), generation)`: `Ready(o)` becomes
  `ByteTake::Ready(o)`, and `Absent { .. }` becomes `ByteTake::Absent`;
- `publish_into`, which keeps its over-long-key check and then calls `inner.publish(ByteKey { op, path }, outcome,
  generation)`;
- `drain`, which calls `inner.drain()`.

- [ ] **Step 3: Port the stream slot**

`file_write_slot.rs` keeps `OpKey`, `OpKind`, `Take` and the orphan list, and replaces its `Answer` struct and
`answer` field:

```rust
/// A stream answer: the `Ok` payload is a handle, byte count or free-cluster count; the `Err` payload is what the
/// failure carries. Only a successful `Open` carries a handle to release.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct StreamAnswer {
    kind: OpKind,
    result: Result<u32, u32>,
}

impl deluge_channel::Answer for StreamAnswer {
    fn orphan(&self) -> Option<u32> {
        match (self.kind, self.result) {
            (OpKind::Open, Ok(handle)) => Some(handle),
            _ => None,
        }
    }
}
```

`AnswerSlot` (this module's) holds `inner: deluge_channel::AnswerSlot<OpKey, StreamAnswer>` and
`orphans: heapless::Vec<u32, MAX_ORPHANS>`. Each method calls `inner`'s, and pushes any orphan it returns onto
`orphans`:
- `publish(key, result, generation)` calls `inner.publish(key, StreamAnswer { kind: key.kind, result },
  generation)`, and keeps `key.kind` before moving `key`;
- `take` maps `Ready(StreamAnswer { result: Ok(v), .. })` to `Take::Ready(v)`, `Ready(… Err(p) …)` to
  `Take::Failed(p)`, and `Absent { orphan }` to `Take::Absent` after keeping the orphan.

- [ ] **Step 4: One flush table**

In `src/bsp/rust/src/card.rs`, replace the list of calls in `deluge_card_flush` with:

```rust
/// Every channel that keeps answers read from the card. Each clears its queued requests and waiting answers;
/// none may block, because the ejection path runs this with the card already gone.
const FLUSHES: [fn(); 5] = [
    super::audio_loader::flush_tables,
    super::file_prefix::flush_tables,
    super::file_directory::flush_tables,
    super::file_bytes::flush_tables,
    super::file_write::flush_tables,
];
```

and the body with `for flush in FLUSHES { flush(); }`. Keep `deluge_card_generation::bump()` where it is now,
AHEAD of the table (see the comment on `bump`).

- [ ] **Step 5: Run to verify nothing changed**

Run: `./dbt test bsp-host settings-channel favourites-channel card-reinsert document-save-channel song-save-channel recorder-write`
Expected: PASS, with the same `file_bytes_host` and `file_write_host` counts as Step 1. `git diff --stat
src/bsp/rust/tests` prints nothing.

Then run the device link check. Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/bsp/rust/src/file_bytes.rs src/bsp/rust/src/file_write_slot.rs src/bsp/rust/src/card.rs
git commit -m "refactor: put the byte and stream channels on the shared answer slot, and flush from one table"
```

---

### Task 4: `deluge::io::StallBound`

Twelve C++ state machines hand-roll the same counter: "give up on a channel after 600 ticks of PENDING".

**Files:**
- Create: `src/deluge/io/stall_bound.h`
- Create: `tests/unit/cpputest/stall_bound_tests.cpp`
- Modify, the counter sites:
  - `gui/ui/browser/dx_browser.cpp:64`
  - `gui/ui/browser/browser.cpp:75`
  - `gui/ui/load/load_song_ui.h:238`
  - `gui/ui/load/load_song_ui.cpp:382` (2000 ticks)
  - `gui/ui/save/save_ui.cpp:246`
  - `gui/ui/save/save_song_ui.cpp:412, 656`
  - `processing/stem_export/stem_export.cpp:990`
  - `storage/smsysex_pending.cpp:48`
  - `storage/byte_channel.cpp:250`
  - `storage/startup_song.cpp:84`
  - `storage/audio/async_sample_load.h:92`
  - `storage/smsysex.cpp:40`
  - `storage/file_reader.cpp:287` (`uint16_t`)

**Interfaces:**
- Produces:
  - `deluge::io::kChannelStallTicks` (`int32_t` 600, in caller ticks);
  - `deluge::io::StallBound`, with:
    - `explicit constexpr StallBound(int32_t limit = kChannelStallTicks)`
    - `void reset() noexcept`
    - `bool tick() noexcept`, which counts one tick and returns true once the limit is reached
    - `[[nodiscard]] bool expired() const noexcept`
    - `[[nodiscard]] int32_t ticks() const noexcept`
    - `[[nodiscard]] int32_t limit() const noexcept`

- [ ] **Step 1: Write the failing test**

`tests/unit/cpputest/stall_bound_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "io/stall_bound.h"

using deluge::io::StallBound;

TEST_GROUP(StallBound){};

TEST(StallBound, DefaultsToTheChannelBound) {
	StallBound bound;
	CHECK_EQUAL(deluge::io::kChannelStallTicks, bound.limit());
	CHECK_EQUAL(600, bound.limit());
}

TEST(StallBound, ExpiresOnTheLimitTickAndNotBefore) {
	StallBound bound{3};
	CHECK_FALSE(bound.tick());
	CHECK_FALSE(bound.tick());
	CHECK_FALSE(bound.expired());
	CHECK_TRUE(bound.tick());
	CHECK_TRUE(bound.expired());
	CHECK_EQUAL(3, bound.ticks());
}

TEST(StallBound, StaysExpiredAndStopsCounting) {
	StallBound bound{1};
	CHECK_TRUE(bound.tick());
	CHECK_TRUE(bound.tick());
	CHECK_EQUAL(1, bound.ticks());
}

TEST(StallBound, ResetStartsOver) {
	StallBound bound{2};
	bound.tick();
	bound.tick();
	bound.reset();
	CHECK_FALSE(bound.expired());
	CHECK_EQUAL(0, bound.ticks());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `./dbt test unit`
Expected: FAIL: `io/stall_bound.h: No such file or directory`.

- [ ] **Step 3: Write it**

`src/deluge/io/stall_bound.h`:

```cpp
#pragma once

#include <cstdint>

namespace deluge::io {

/// How many caller ticks a request may stay PENDING before its caller gives up on the channel.
///
/// A channel whose driver never runs answers PENDING forever, which is neither a refusal nor an answer. Without a
/// bound, a caller would poll until the end of time. Six hundred ticks is several seconds at the UI's tick rate:
/// far beyond any real operation, far short of forever.
inline constexpr int32_t kChannelStallTicks = 600;

/// Counts the ticks a caller spends waiting on one channel request, and says when to give up.
class StallBound {
public:
	explicit constexpr StallBound(int32_t limit = kChannelStallTicks) noexcept : limit_{limit} {}

	/// Start counting again, for a new request.
	void reset() noexcept { ticks_ = 0; }

	/// Count one tick spent waiting. True once the limit is reached, and on every tick after.
	bool tick() noexcept {
		if (ticks_ < limit_) {
			ticks_++;
		}
		return expired();
	}

	[[nodiscard]] bool expired() const noexcept { return ticks_ >= limit_; }
	[[nodiscard]] int32_t ticks() const noexcept { return ticks_; }
	[[nodiscard]] int32_t limit() const noexcept { return limit_; }

private:
	int32_t limit_;
	int32_t ticks_ = 0;
};

} // namespace deluge::io
```

- [ ] **Step 4: Run to verify it passes**

Run: `./dbt test unit`
Expected: PASS.

- [ ] **Step 5: Migrate the sites**

At each site:
- replace the constant and its counter with a `deluge::io::StallBound` member or file-scope object;
- replace `if (++counter >= kMax…)` or `counter++; if (counter > kMax…)` with `if (bound.tick())`;
- replace `counter = 0` with `bound.reset()`.

Keep each site's comment only where it says something specific to that site. The generic "why a bound" text now
lives on `kChannelStallTicks`.

Before editing a site, read its comparison:
- `>=` after an increment is `tick()`'s rule.
- `>` after an increment gives up one tick later. Keep that site's behaviour with `StallBound{kChannelStallTicks
  + 1}` and a one-line comment saying the gate that pins it. Only do this if a gate's marker checks the exact
  count (for example `SP-LISTING-STALL` or `DS-ACCOUNTED`). Otherwise use the default and let the gates confirm.

`load_song_ui.cpp:382`'s staging wait keeps its own limit: `StallBound{kMaxStagingWaitTicks}`, with
`kMaxStagingWaitTicks = 2000` and its existing comment. `file_reader.cpp`'s `uint16_t` counter becomes the
`int32_t` bound.

Where a gate prints a site's tick count (the `*-STALL` legs and `DS-ACCOUNTED`), print `bound.ticks()`.

- [ ] **Step 6: Run the gates that hold the stall paths**

Run: `./dbt test unit song-preview-ab document-save-channel song-save-channel sample-load-tree sample-claim preset-load pattern-load startup-failsafe stem-export settings-channel`
Expected: PASS. These hold the `*-STALL` legs.

Run: `git grep -n 'kMax[A-Za-z]*Ticks\s*=' -- src/deluge`
Expected: only `kMaxStagingWaitTicks`, and any site kept at 601 with its comment.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/io/stall_bound.h tests/unit/cpputest/stall_bound_tests.cpp src/deluge
git commit -m "refactor: give every channel caller one stall bound"
```

---

### Task 5: Whole-file reads and writes join the byte channel

`deluge_file_write_request_read_file`/`write_file` are path-keyed operations on the handle-keyed stream channel.
They move to the byte channel, which is path-keyed and single-slot, as caller-buffer operations. There, a failure
carries a `DelugeStatus`.

**Files:**
- Modify: `include/libdeluge/file_bytes.h`: add `deluge_file_bytes_request_read_into`,
  `deluge_file_bytes_collect_read_into`, `deluge_file_bytes_request_write_from`,
  `deluge_file_bytes_collect_write_from` and `deluge_file_bytes_holds`.
- Modify: `src/bsp/rust/src/file_bytes.rs`:
  - `ByteOp::{ReadInto, WriteFrom}`;
  - `ByteRequest::{ReadInto { path, dst, cap }, WriteFrom { path, src, len, may_overwrite }}`;
  - `ByteOutcome::{Filled(u32), Wrote(u32)}` and `ByteOutcome::Refused { status: i8, needed: u32 }`;
  - `ByteOps::{read_into, write_new}`;
  - the exports, with their `abi_pin!`;
  - a `RUNNING_BUFFER` like the stream channel's.
- Modify: `src/bsp/rust/src/file_write.rs` (delete `ReadFile`/`WriteFile` and their four exports, and move their
  task arms into `ProdByteOps`)
- Modify: `include/libdeluge/file_write.h` (delete those four declarations)
- Modify, the callers:
  - `src/deluge/gui/ui/save/save_ui.cpp:351`
  - `src/deluge/gui/ui/save/save_song_ui.cpp:803`
  - `src/deluge/storage/file_reader.cpp:348`
  - `src/deluge/storage/startup_song.cpp:329`
  - their collects, and every `deluge_file_write_holds` caller (`git grep -n deluge_file_write_holds`)
- Modify: `tests/support/mocks/storage_channel_mocks.cpp` (the `holds` mock moves with the name)
- Test: `src/bsp/rust/tests/file_bytes_host.rs` (new cases for the request-acceptance rule)

**Interfaces:**
- Produces:

```c
/// Ask for the whole of `path` to be read into `dst`, which the caller owns and keeps alive and untouched until the
/// collect is terminal (see deluge_file_bytes_holds). Refused (false) while another byte request is queued. [task]
bool deluge_file_bytes_request_read_into(const char* path, void* dst, uint32_t cap) DELUGE_NOEXCEPT;
/// READY: `*out_len` bytes are in `dst`. FAILED: `*out_error` says why; DELUGE_ERR_NO_MEMORY means the file did not
/// fit, and `*out_needed` is its size. Any out-pointer may be null. [task]
DelugeChannelStatus deluge_file_bytes_collect_read_into(const char* path, uint32_t* out_len, DelugeStatus* out_error,
                                                        uint32_t* out_needed) DELUGE_NOEXCEPT;
/// Ask for `len` bytes at `src` to be written as the whole of `path`. With `may_overwrite` false, the file must not
/// exist; that check and the create are one atomic open. `src` is the caller's until the collect is terminal.
/// Refused (false) while another byte request is queued. [task]
bool deluge_file_bytes_request_write_from(const char* path, const void* src, uint32_t len, bool may_overwrite) DELUGE_NOEXCEPT;
/// READY: all `*out_written` bytes are on the card. FAILED: `*out_error` says why; DELUGE_ERR_EXISTS means the name
/// was taken and the existing file is untouched. [task]
DelugeChannelStatus deluge_file_bytes_collect_write_from(const char* path, uint32_t* out_written,
                                                         DelugeStatus* out_error) DELUGE_NOEXCEPT;
/// Whether a queued or running byte request reads or writes through `buffer`. True when it cannot tell. [task]
bool deluge_file_bytes_holds(const void* buffer) DELUGE_NOEXCEPT;
```

- [ ] **Step 1: Write the failing tests**

The byte channel's `enqueue_request` decides acceptance, and it is pure over the `REQUEST` slot. Make the
acceptance rule a pure function, and test it. Add to `src/bsp/rust/tests/file_bytes_host.rs`:

```rust
use file_bytes::{accepts, ByteOp};

#[test]
fn a_caller_buffer_request_is_refused_while_another_is_queued() {
    assert!(accepts(None, ByteOp::WriteFrom));
    assert!(!accepts(Some(ByteOp::Read), ByteOp::WriteFrom));
    assert!(!accepts(Some(ByteOp::WriteFrom), ByteOp::ReadInto));
}

#[test]
fn a_second_write_from_is_refused_not_replaced() {
    assert!(!accepts(Some(ByteOp::WriteFrom), ByteOp::WriteFrom));
}

#[test]
fn the_existing_read_rule_is_unchanged() {
    // Record here what `enqueue_request` does today for a plain Read over a queued Read, before changing it; the
    // assertion below is today's rule, whichever it is.
    assert_eq!(accepts(Some(ByteOp::Read), ByteOp::Read), file_bytes::READ_REPLACES_READ);
}
```

In `file_bytes.rs`, find what `enqueue_request` does today when a request is already queued, and define
`pub const READ_REPLACES_READ: bool` to match. `true` means a new request replaces a queued one; `false` means it
is refused. The third test then pins today's rule.

- [ ] **Step 2: Run to verify it fails**

Run: `./dbt test bsp-host`
Expected: FAIL. `accepts`, `ByteOp::WriteFrom`, `ByteOp::ReadInto` and `READ_REPLACES_READ` are not found.

- [ ] **Step 3: The acceptance rule, the new operations, and their driver arms**

In `file_bytes.rs`:

```rust
/// Whether a new request of kind `new` may enter the request slot while `queued` is in it.
///
/// A request that hands over a caller's buffer is never replaced and never replaces. Its caller must see a refusal,
/// because replacing a queued document write loses a save, and replacing another request with one would drop that
/// request silently.
pub fn accepts(queued: Option<ByteOp>, new: ByteOp) -> bool {
    match queued {
        None => true,
        Some(q) if caller_buffer(q) || caller_buffer(new) => false,
        Some(_) => READ_REPLACES_READ,
    }
}

fn caller_buffer(op: ByteOp) -> bool {
    matches!(op, ByteOp::ReadInto | ByteOp::WriteFrom)
}
```

Make `enqueue_request` use `accepts`. Its behaviour for the existing kinds must not change; the third test and the
settings gates hold this.

Add the two ops, the request variants (their pointers stored as `usize`, as the stream channel stores them), and
the outcomes. Add the trait methods:

```rust
    /// Read the whole of `path` into `into`. A file larger than `into` is refused with `NO_MEMORY` and its size;
    /// nothing is read. A short read of a whole file is `IO`: the caller would parse a truncated document.
    async fn read_into(&self, path: &str, into: &mut [u8]) -> Result<u32, (i8, u32)>;
    /// Write `bytes` as the whole of `path`. With `exclusive`, the create fails with `EXISTS` if the file is there,
    /// in the same open, so no other writer can slip in between a check and the create. A short write is `IO`.
    async fn write_new(&self, path: &str, bytes: &[u8], exclusive: bool) -> Result<u32, i8>;
```

Implement them on `ProdByteOps` by moving the `StreamOp::ReadFile` and `StreamOp::WriteFile` arms from
`file_write_task_body` (P1's version), with these changes:
- `Err(size)` and `Err(0)` become `Err((NO_MEMORY, size))` and `Err((IO, 0))` for `read_into`;
- in `write_new`, the refusal classification (`exists`) becomes `Err(EXISTS)` or `Err(IO)`;
- the backing calls use `file_bytes`'s own `backing` module. Add `stream_open`, `stream_size`,
  `stream_read_at_via`, `stream_write_at` and `stream_close` to its re-export list if they are not there.

Add the driver arms in `bytes_io_once`. They build the slices from the stored addresses under the same SAFETY
contract the stream channel documents, and map the results to `ByteOutcome::Filled(n)`, `Wrote(n)` or
`Refused { status, needed }`.

Add `RUNNING_BUFFER: AtomicUsize`, set where the task takes a request and cleared once the operation has finished
with the buffer, as `file_write.rs` does. Add `deluge_file_bytes_holds`, which is `file_write.rs`'s
`deluge_file_write_holds` with `op_buffer` reading the byte request's pointer.

Add the five exports, with an `abi_pin!` for each. A collect maps `ByteTake::Ready(Filled(n))` to READY and
`*out_len = n`. It maps `Ready(Refused { status, needed })` to FAILED, with `*out_error = status` and
`*out_needed = needed`. `Absent` falls through to the queued/running check the other collects already make.

- [ ] **Step 4: Run to verify it passes**

Run: `./dbt test bsp-host`
Expected: PASS, with the three new tests listed.

- [ ] **Step 5: Move the callers, and delete the stream channel's whole-file operations**

- Replace each `deluge_file_write_request_read_file(path, buf, cap)` with
  `deluge_file_bytes_request_read_into(path, buf, cap)`, and its collect with `…collect_read_into(path, &len,
  &error, &needed)`.
- Where a caller distinguished "too small" by `needed != 0`, it now tests `error == DELUGE_ERR_NO_MEMORY`.
- Replace each `request_write_file(path, bytes, len, mayOverwrite)` with `request_write_from(…)` and its collect.
  Where a caller read `out_exists`, it now tests `error == DELUGE_ERR_EXISTS`.
- Replace `deluge_file_write_holds` with `deluge_file_bytes_holds` at every call and in the mock.

Delete `StreamOp::ReadFile`/`WriteFile`, `OpKind::ReadFile`/`WriteFile`, their arms, their four exports and
declarations, and `file_write_slot`'s now-unused path key. `OpKey::path` stays only if Task 6 needs it for
`Open`, and it does.

Run: `git grep -n 'deluge_file_write_\(request\|collect\)_\(read\|write\)_file\|deluge_file_write_holds' -- ':!docs/superpowers'`
Expected: no output.

- [ ] **Step 6: Run the gates**

Run: `./dbt test bsp-host unit document-save-channel song-save-channel song-load-channel save-roundtrip staging-release startup-failsafe settings-channel favourites-channel`
Expected: PASS. DS-OCCUPIED, DS-OVERWRITE-FAIL and SS-INTACT hold the EXISTS and intact-on-failure rules.
staging-release holds `holds()`.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0.

Then run the device link check. Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add include/libdeluge/file_bytes.h include/libdeluge/file_write.h src/bsp/rust/src src/bsp/rust/tests/file_bytes_host.rs src/deluge tests/support/mocks/storage_channel_mocks.cpp
git commit -m "refactor: move whole-file reads and writes to the byte channel, which says why one failed"
```

---

### Task 6: `file_write` becomes `file_stream`, and its open says why it failed

What remains of the write channel is the recorder's handle-keyed stream channel. It gets its own name, and its open
is keyed by path and reports a `DelugeStatus`. The recorder's folder retry depends on that ("The channel cannot say
NOT_FOUND apart from any other failure", `sample_recorder.cpp:1180`).

**Files:**
- Rename: `include/libdeluge/file_write.h` to `file_stream.h`, `src/bsp/rust/src/file_write.rs` to
  `file_stream.rs`, `file_write_slot.rs` to `file_stream_slot.rs`, and `tests/file_write_host.rs` to
  `file_stream_host.rs` (with `git mv`)
- Modify: every `deluge_file_write_*` and `DELUGE_FILE_WRITE_*` user. Find them with
  `git grep -l 'deluge_file_write_\|DELUGE_FILE_WRITE_\|file_write\.h\|file_write::\|mod file_write'`.
- Modify: `src/deluge/io/stream.cpp`/`stream.hpp` (`request_open`/`collect_open`, which now carry the path)
- Modify: `src/deluge/model/sample/sample_recorder.cpp:811, 1173, 1501` (pass the path the open was requested
  with, and read the status)
- Modify: `src/bsp/rust/src/efatfs_fs.rs` and `efatfs_host_shim.rs` (`stream_open` returns `Result<u32, i8>`)

**Interfaces:**
- Produces:
  - every `deluge_file_write_X` renamed to `deluge_file_stream_X`, and every `DELUGE_FILE_WRITE_Y` to
    `DELUGE_FILE_STREAM_Y`, with the same signatures, except:

```c
DelugeChannelStatus deluge_file_stream_collect_open(const char* path, uint32_t* out_handle, DelugeStatus* out_error) DELUGE_NOEXCEPT;
```

  - `deluge::io::Stream::collect_open(std::string_view path, std::optional<Stream>* out, DelugeStatus* error =
    nullptr)`.

- [ ] **Step 1: Write the failing slot test**

Add to `src/bsp/rust/tests/file_write_host.rs` (before the rename, so the failure is visible on the current
names):

```rust
#[test]
fn an_open_collected_with_another_path_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::Open, 0, "SAMPLES/RECORD/REC001.WAV");
    slot.publish(mine.clone(), Ok(3), CARD);
    assert_eq!(slot.take(&key(OpKind::Open, 0, "SAMPLES/RECORD/REC002.WAV"), CARD), Take::Absent);
    assert_eq!(slot.take(&mine, CARD), Take::Ready(3));
}
```

Run: `./dbt test bsp-host`
Expected: PASS already. The slot compares paths for every kind, and only `op_key` keys `Open` by `""`. This test
pins the slot rule the next steps rely on. The failing step is the next one.

- [ ] **Step 2: Key the open by path, and make it report a status**

- In `file_write.rs`, `op_key` keys `StreamOp::Open` by `OpKey::new(OpKind::Open, 0, path)`.
- `deluge_file_write_collect_open` gains `path: *const c_char` and `out_error: *mut i8`.
- On FAILED it writes the status the open failed with. The `Err` payload of an `Open` answer becomes that status,
  as `u32::from(status as u8)`, and is converted back to `i8` on collect.
- In the header, the declaration gains the two parameters.

In `efatfs_fs.rs` and `efatfs_host_shim.rs`, make `stream_open` return `Result<u32, i8>` in place of `Option<u32>`
(the status it currently collapses with `.ok()`), and update its callers. `open_mounted` retries the mount only on
`NO_FILESYSTEM` and returns the final status.

In `stream.cpp`, `collect_open(std::string_view path, std::optional<Stream>* out, DelugeStatus* error)` passes
`std::string(path).c_str()` and forwards `error`.

In `sample_recorder.cpp`:
- **At 1173 (create):** pass `filePathCreated`, and retry the folder only when `error == DELUGE_ERR_NOT_FOUND`.
  Replace the comment "The channel cannot say NOT_FOUND apart from any other failure…" with "Only a missing folder
  is worth the retry: any other failure fails the recording."
- **At 1501 (finalize's reopen):** pass `sample->filePath`.
- **At 811 (abort):** pass the path of whichever open is in flight. Record it when the open is requested (a
  `std::string openPath_` member set beside each `request_open`), since abort does not otherwise know which open
  it is draining.

Run: `./dbt test bsp-host recorder-write`
Expected: PASS.

Neutering check: at 1173, retry on any FAILED again. Hold the folder open failure as a card error: the recorder
gate has no such leg, so add `RW-OPEN-IOERR` to the recorder scenario. It uses the write channel's host latency
knob and `deluge_efatfs_unmount()` to make the open fail with `NO_FILESYSTEM`, and requires the recording to fail
without creating a folder. It must FAIL with the neutered retry and PASS with the real one. Add `RW-OPEN-IOERR` to
the `recorder-write` entry's `markers` and `gates` text in `tests/registry.toml`.

- [ ] **Step 3: Rename**

```bash
git mv include/libdeluge/file_write.h include/libdeluge/file_stream.h
git mv src/bsp/rust/src/file_write.rs src/bsp/rust/src/file_stream.rs
git mv src/bsp/rust/src/file_write_slot.rs src/bsp/rust/src/file_stream_slot.rs
git mv src/bsp/rust/tests/file_write_host.rs src/bsp/rust/tests/file_stream_host.rs
git grep -l 'deluge_file_write_\|DELUGE_FILE_WRITE_\|file_write\.h\|file_write_slot\|file_write::\|mod file_write\|LIBDELUGE_FILE_WRITE_H' -- ':!docs/superpowers' | \
  xargs sed -i -e 's/deluge_file_write_/deluge_file_stream_/g' -e 's/DELUGE_FILE_WRITE_/DELUGE_FILE_STREAM_/g' \
    -e 's/file_write\.h/file_stream.h/g' -e 's/file_write_slot/file_stream_slot/g' -e 's/file_write::/file_stream::/g' \
    -e 's/mod file_write/mod file_stream/g' -e 's/LIBDELUGE_FILE_WRITE_H/LIBDELUGE_FILE_STREAM_H/g'
```

Then read `git diff --stat` and check:
- the renamed header's own comments say "stream channel", not "write channel";
- `card.rs`'s table names `super::file_stream::flush_tables`;
- `file_write_task` is now `file_stream_task` at its spawn site in `main.rs`.

- [ ] **Step 4: Run everything that touches it**

Run: `./dbt test bsp-host unit recorder-write staging-release card-reinsert golden-vt-render`
Expected: PASS.

Run: `scripts/check_abi_surface.py --check --pins`
Expected: exit 0.

Then run the device link check. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add -A include/libdeluge src/bsp/rust src/deluge tests
git commit -m "refactor: name the recorder's channel the stream channel, key its open by path, and report why an open failed"
```

---

### Task 7: Whole-suite check and merge

- [ ] **Step 1:** Run `./dbt test`.
  Expected: every entry PASS, apart from entries whose status is `unavailable`.
- [ ] **Step 2:** Run the device link check.
  Expected: exit 0.
- [ ] **Step 3:** `git switch next && git merge --no-ff refactor/libdeluge-channels`
