# libdeluge ABI P1: live defects — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four defects the ABI audits found: noisy capture stamps, the dead trigger-clock input, the write channel's loose and eject-blind answer slot, and `FileTimestamp` crossing the ABI with Rust layout.

**Architecture:**
- Capture stamps become TX play frames, computed by a pure, host-tested Rust function.
- Trigger-clock edges reach the app through the existing `deluge_trigger_clock_set_handler`, into a small app-owned
  ring.
- The write channel's answer slot moves into a pure module that follows `file_bytes`'s slot rules (key-mismatch
  leaves the answer alone, a stale generation removes it) and records which opened handles need closing.
- `FileTimestamp` gets `#[repr(C)]` and layout asserts on both sides.

**Tech Stack:** Rust (Embassy, heapless) in `src/bsp/rust`; C++23 app in `src/deluge`; CppUTest; the scenario gates.

**Spec:** `docs/superpowers/specs/2026-09-29-libdeluge-abi-overhaul-design.md` (§2 A1, A1b, A3, A3b; §3 decisions 1–3)

## Global Constraints

- Branch off `next`: `git switch -c fix/libdeluge-abi-bugs next`. Local merges go into `next`, never `main`. Never push.
- Commit after every task, without asking. No `Co-Authored-By`. Commit subjects name the change, not the process.
- If a pre-commit hook (clang-format, rustfmt, ruff) reformats files, re-add them and commit again. Never `--amend`.
- Comments state the current rationale only: no history, no "was", no decision records.
- New C++ is idiomatic C++23. Document the units of every fixed-point or time value.
- An `#include` folded onto the previous line is silently dropped. Check every include you add is on its own line.
- **Device link check** (run where a task says so):
  ```bash
  mv src/bsp/rust/local-sdk.toml /tmp/local-sdk.toml.held 2>/dev/null; ./dbt build RelWithDebInfo; rc=$?; mv /tmp/local-sdk.toml.held src/bsp/rust/local-sdk.toml 2>/dev/null; git checkout -q src/bsp/rust/Cargo.lock; exit $rc
  ```
- Test output is filtered to marker lines. Judge a run by its exit status and its PASS/FAIL summary lines.
- Hardware never gates a task.

## Review Focus

- **A stamp read long after its event.** An edge the clock task reads more than one TX ring (1024 frames, 23 ms)
  late must clamp to the oldest representable frame, not alias to a recent one. Tested in Task 1:
  `a_late_event_clamps_to_the_ring`.
- **Two edges between two wakes of the clock task.** Both carry the last edge's tick stamp, which is existing
  behaviour. Neither may be dropped while the ring has room. Tested in Task 2:
  `two_pushes_before_a_pop_both_arrive`.
- **More edges than the ring holds.** The edges past capacity are counted as dropped. The waiting edges are not
  overwritten. Tested in Task 2: `a_full_ring_drops_the_newest_and_counts_it`.
- **A collect with another file's path.** A whole-file read collected with a different path must answer UNKNOWN
  and leave the real answer for its owner. Tested in Task 3: `a_wrong_path_leaves_the_answer`.
- **An open answer nobody collects.** Its handle must be closed, not leaked from the 8-slot table, whether it was
  displaced, discarded, stale or flushed. Tested in Task 3 by four orphan tests.

---

### Task 1: Capture stamps on the TX play timeline

A capture stamp is the TX ring's play frame at the moment of capture. Today the producers write Embassy `Instant`
ticks and `deluge_audio_stamp_to_render_offset` masks `stamp - TX_WRITE`. The result is a pseudo-random offset on
every MIDI clock tick.

**Files:**
- Create: `src/bsp/rust/src/capture_stamp.rs`
- Create: `src/bsp/rust/tests/capture_stamp_host.rs`
- Modify: `src/bsp/rust/src/main.rs`: declare `mod capture_stamp;` unconditionally, next to `mod audio;` (line 133).
- Modify: `src/bsp/rust/src/audio.rs:88` (`tx_play_frame` becomes `pub(crate)`) and `:268`
  (`deluge_audio_stamp_to_render_offset`).
- Modify: `src/bsp/rust/src/midi.rs:161` (the stamp `deluge_midi_din_read_timed` writes).
- Modify: `src/bsp/rust/src/cv_gate.rs:101` (the stamp the handler gets).
- Modify: `include/libdeluge/audio_io.h:95-100`, `include/libdeluge/midi_io.h` (the `deluge_midi_din_read_timed`
  doc), and `include/libdeluge/cv_gate.h` (the `DelugeTriggerClockHandler` doc).

**Interfaces:**
- Produces:
  - `capture_stamp::stamp_at(play_now: u32, now_ticks: u64, event_ticks: u64, tick_hz: u64, sample_rate: u32,
    max_frames: u32) -> u32`
  - `capture_stamp::render_offset(stamp: u32, block_start_play: u32, mask: u32) -> u32`
  - `capture_stamp::now_for(event_ticks: u64) -> u32`: the device's play position, or 0 on the host.
  - Semantics: `DelugeTriggerClockHandler`'s argument and `deluge_midi_din_read_timed`'s `arrival_stamp` are
    capture stamps.

- [ ] **Step 1: Write the failing test**

`src/bsp/rust/tests/capture_stamp_host.rs`:

```rust
//! Capture stamps: where on the TX play timeline an input event happened.
//!
//! `deluge-bsp-rust` is bin-only, so this recompiles `src/capture_stamp.rs` into the test binary via `#[path]`,
//! the convention the other `*_host.rs` tests use.
#![cfg(not(target_os = "none"))]

#[path = "../src/capture_stamp.rs"]
mod capture_stamp;

use capture_stamp::{render_offset, stamp_at};

/// 1 MHz, the device's Embassy tick rate.
const TICK_HZ: u64 = 1_000_000;
const RATE: u32 = 44_100;
/// The TX ring is 1024 frames.
const MASK: u32 = 1023;

#[test]
fn an_event_read_at_once_is_stamped_with_the_play_position() {
    assert_eq!(stamp_at(500, 7_000, 7_000, TICK_HZ, RATE, MASK), 500);
}

#[test]
fn an_event_one_millisecond_old_is_stamped_44_frames_back() {
    assert_eq!(stamp_at(500, 8_000, 7_000, TICK_HZ, RATE, MASK), 456);
}

#[test]
fn a_stamp_before_the_ring_start_wraps_and_still_converts() {
    let stamp = stamp_at(10, 8_000, 7_000, TICK_HZ, RATE, MASK);
    // 44 frames before play frame 10 is frame 990 of the previous lap.
    assert_eq!(render_offset(stamp, 990, MASK), 0);
    assert_eq!(render_offset(stamp, 980, MASK), 10);
}

#[test]
fn spacing_between_events_survives_reading_them_at_different_times() {
    // Two edges 10 ms apart (441 frames); the first read 3 ms late, the second 1 ms late.
    let first = stamp_at(132 + 132, 13_000, 10_000, TICK_HZ, RATE, MASK);
    let second = stamp_at(132 + 441 + 44, 21_000, 20_000, TICK_HZ, RATE, MASK);
    let block_start = 100;
    let a = render_offset(first, block_start, MASK);
    let b = render_offset(second, block_start, MASK);
    assert_eq!(b.wrapping_sub(a) & MASK, 441);
}

#[test]
fn an_event_stamped_in_the_future_is_treated_as_now() {
    assert_eq!(stamp_at(500, 7_000, 9_000, TICK_HZ, RATE, MASK), 500);
}

#[test]
fn a_late_event_clamps_to_the_ring() {
    // One second late is 44,100 frames: more than the ring holds. It clamps to MASK frames back.
    assert_eq!(stamp_at(500, 1_007_000, 7_000, TICK_HZ, RATE, MASK), 500u32.wrapping_sub(MASK));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./dbt test bsp-host`
Expected: FAIL. `capture_stamp_host` does not compile: the file `src/capture_stamp.rs` is not found.

- [ ] **Step 3: Write the module**

`src/bsp/rust/src/capture_stamp.rs`:

```rust
//! Capture stamps: where on the TX play timeline an input event happened.
//!
//! A capture stamp is the TX ring's play frame at the moment of the event, in frames at the codec rate, modulo
//! the ring. Every input the app times (a MIDI byte, a trigger-clock edge) is stamped on this one timeline, so
//! `deluge_audio_stamp_to_render_offset` can place it in the next render. Each event is late by the same block,
//! and the spacing between events is exact.

/// The stamp for an event that happened at `event_ticks`, read when the play position is `play_now` and the
/// Embassy clock reads `now_ticks`. Ticks are at `tick_hz`, frames at `sample_rate`.
///
/// An event from the future (a clock read out of order) counts as now. An event more than `max_frames` old
/// clamps to `max_frames` back, because the ring cannot place anything older.
pub fn stamp_at(
    play_now: u32,
    now_ticks: u64,
    event_ticks: u64,
    tick_hz: u64,
    sample_rate: u32,
    max_frames: u32,
) -> u32 {
    let elapsed_ticks = now_ticks.saturating_sub(event_ticks);
    let elapsed_frames = elapsed_ticks.saturating_mul(u64::from(sample_rate)) / tick_hz;
    let back = elapsed_frames.min(u64::from(max_frames)) as u32;
    play_now.wrapping_sub(back)
}

/// How far past `block_start_play` (the play frame when the block being rendered began) the event at `stamp`
/// fell, in frames, modulo the ring (`mask` + 1 frames).
pub fn render_offset(stamp: u32, block_start_play: u32, mask: u32) -> u32 {
    stamp.wrapping_sub(block_start_play) & mask
}

/// The stamp for an event at `event_ticks`, read now.
#[cfg(target_os = "none")]
pub fn now_for(event_ticks: u64) -> u32 {
    stamp_at(
        crate::audio::tx_play_frame() as u32,
        embassy_time::Instant::now().as_ticks(),
        event_ticks,
        embassy_time::TICK_HZ,
        crate::audio::SAMPLE_RATE_HZ,
        crate::audio::TX_MASK as u32,
    )
}

/// The Embassy host's render has no play position to read (the audio device paces it), so every event is "now".
/// `deluge_audio_stamp_to_render_offset` answers 0 there too.
#[cfg(not(target_os = "none"))]
pub fn now_for(_event_ticks: u64) -> u32 {
    0
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./dbt test bsp-host`
Expected: PASS, with the six `capture_stamp_host` tests listed.

- [ ] **Step 5: Wire the device side**

In `src/bsp/rust/src/audio.rs`:
- Make `fn tx_play_frame()` (line 88) `pub(crate) fn tx_play_frame()`.
- Make `const TX_MASK` (line 41) `pub(crate) const TX_MASK`.
- Add, next to `TX_FRAMES`:
  ```rust
  /// The codec rate, in frames per second.
  pub(crate) const SAMPLE_RATE_HZ: u32 = 44_100;
  ```
- Make `deluge_audio_sample_rate` return `SAMPLE_RATE_HZ`.
- Replace `deluge_audio_stamp_to_render_offset` with:

```rust
/// The frame, within the block being rendered, at which the event stamped `stamp` lands. See `capture_stamp`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stamp_to_render_offset(stamp: u32) -> u32 {
    // SAFETY: BLOCK_START_PLAY is written only by the audio tier's render, which is where this is called from.
    let block_start = unsafe { BLOCK_START_PLAY } as u32;
    crate::capture_stamp::render_offset(stamp, block_start, TX_MASK as u32)
}
```

In `src/bsp/rust/src/midi.rs:161`, replace the tick write:

```rust
                    *arrival_ticks = crate::capture_stamp::now_for(embassy_time::Instant::now().as_ticks());
```

Rename the parameter `arrival_ticks` to `arrival_stamp` in `midi.rs`, `host_link_stubs.rs:148` and the header. In
`host_link_stubs.rs` write `crate::capture_stamp::now_for(0)`, which is 0, in place of the tick count.

In `src/bsp/rust/src/cv_gate.rs:101`:

```rust
        let ts = u64::from(crate::capture_stamp::now_for(
            trigger_clock::LAST_EDGE_TICKS.load(Ordering::Relaxed),
        ));
```

- [ ] **Step 6: Say it in the headers**

Replace the `deluge_audio_stamp_to_render_offset` doc in `include/libdeluge/audio_io.h:95-99` with:

```c
/// Convert a capture stamp into the frame, within the block being rendered, at which the event lands.
///
/// A capture stamp is the TX ring's play frame at the moment of the event: the `arrival_stamp` of
/// `deluge_midi_din_read_timed()`, or a trigger-clock handler's argument. Every event lands one block late, by the
/// same amount, so the spacing between events is exact. On the Embassy host every stamp converts to 0 ("now").
/// [audio]
```

In `midi_io.h`, name the parameter `arrival_stamp` and document it as "the event's capture stamp; see
`deluge_audio_stamp_to_render_offset`". In `cv_gate.h`, document `DelugeTriggerClockHandler`'s argument as "the
edge's capture stamp (see `deluge_audio_stamp_to_render_offset`), widened to 64 bits".

- [ ] **Step 7: Build everything that links it**

Run: `./dbt test bsp-host unit golden-vt-render`
Expected: PASS.

Then run the device link check from Global Constraints.
Expected: exit 0.

- [ ] **Step 8: Commit**

```bash
git add src/bsp/rust/src/capture_stamp.rs src/bsp/rust/tests/capture_stamp_host.rs src/bsp/rust/src/main.rs \
  src/bsp/rust/src/audio.rs src/bsp/rust/src/midi.rs src/bsp/rust/src/cv_gate.rs src/bsp/rust/src/host_link_stubs.rs \
  include/libdeluge/audio_io.h include/libdeluge/midi_io.h include/libdeluge/cv_gate.h
git commit -m "fix: stamp MIDI and clock input on the TX play timeline"
```

---

### Task 2: Trigger-clock edges reach the app

`playback_handler.cpp` polls three globals that only `ffi_extra.rs` defines and nothing writes. The BSP already
calls a handler per edge, but nothing installs one. This task installs a handler that writes into an app-owned
ring, and deletes the globals.

**Files:**
- Create: `src/deluge/playback/trigger_clock_input.h` (the header-only ring, so the unit tests need no BSP)
- Create: `src/deluge/playback/trigger_clock_input.cpp` (the global and the handler installer)
- Create: `tests/unit/cpputest/trigger_clock_input_tests.cpp`
- Modify: `src/deluge/playback/playback_handler.cpp:122-124` (delete the externs) and `:146-151` (drain the ring)
- Modify: `src/deluge/processing/engines/cv_engine.cpp:47` (install beside `deluge_gate_init()`)
- Modify: `src/bsp/rust/src/ffi_extra.rs:50-56` (delete the three trigger-clock globals)
- Modify: `src/board_config.h:88` (delete `TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED`)
- Modify: `src/bsp/rust/src/host_link_stubs.rs` and `include/libdeluge/cv_gate.h` (the host's handler, and a
  host-only edge injector)
- Create: `tests/integration/scenarios/clock_input_scenario.{h,cpp}`
- Modify: `tests/integration/scenarios/scenario_table.cpp` and `tests/registry.toml` (the `clock-input` gate)

**Interfaces:**
- Consumes (from Task 1): the handler's `uint64_t` argument is a capture stamp.
- Produces: `deluge::playback::TriggerClockEdges` (with `push(uint32_t) noexcept`,
  `pop() noexcept -> std::optional<uint32_t>`, `dropped() const noexcept -> uint32_t` and `kCapacity = 4`), the
  global `deluge::playback::triggerClockEdges`, and `deluge::playback::installTriggerClockInput() noexcept`.
- Produces (host only): `void deluge_trigger_clock_host_edge(uint64_t stamp)`, declared under `DELUGE_HOST`.

- [ ] **Step 1: Write the failing test**

`tests/unit/cpputest/trigger_clock_input_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "playback/trigger_clock_input.h"

using deluge::playback::TriggerClockEdges;

TEST_GROUP(TriggerClockEdges){};

TEST(TriggerClockEdges, AnEmptyRingPopsNothing) {
	TriggerClockEdges edges;
	CHECK_FALSE(edges.pop().has_value());
}

TEST(TriggerClockEdges, EdgesPopInArrivalOrder) {
	TriggerClockEdges edges;
	edges.push(10);
	edges.push(20);
	CHECK_EQUAL(10u, edges.pop().value());
	CHECK_EQUAL(20u, edges.pop().value());
	CHECK_FALSE(edges.pop().has_value());
}

TEST(TriggerClockEdges, TwoPushesBeforeAPopBothArrive) {
	TriggerClockEdges edges;
	edges.push(7);
	edges.push(7);
	CHECK_EQUAL(7u, edges.pop().value());
	CHECK_EQUAL(7u, edges.pop().value());
	CHECK_EQUAL(0u, edges.dropped());
}

TEST(TriggerClockEdges, AFullRingDropsTheNewestAndCountsIt) {
	TriggerClockEdges edges;
	for (uint32_t i = 0; i < TriggerClockEdges::kCapacity + 2; i++) {
		edges.push(i);
	}
	CHECK_EQUAL(2u, edges.dropped());
	for (uint32_t i = 0; i < TriggerClockEdges::kCapacity; i++) {
		CHECK_EQUAL(i, edges.pop().value());
	}
	CHECK_FALSE(edges.pop().has_value());
}

TEST(TriggerClockEdges, TheRingKeepsWorkingPastCounterWrap) {
	TriggerClockEdges edges;
	for (uint32_t i = 0; i < 1000; i++) {
		edges.push(i);
		CHECK_EQUAL(i, edges.pop().value());
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./dbt test unit`
Expected: FAIL. The build stops at `playback/trigger_clock_input.h: No such file or directory`.

- [ ] **Step 3: Write the ring**

`src/deluge/playback/trigger_clock_input.h`:

```cpp
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace deluge::playback {

/// Rising edges from the trigger-clock input, carried from the BSP's handler to PlaybackHandler::routine().
///
/// One producer (the BSP's trigger-clock task, through the handler) and one consumer (routine()). Each entry is
/// the edge's capture stamp: the TX play frame at the edge (see `deluge_audio_stamp_to_render_offset`).
class TriggerClockEdges {
public:
	/// Edges that can wait between two routine() calls. A power of two.
	static constexpr uint32_t kCapacity = 4;

	/// Record an edge. When kCapacity edges are already waiting, this edge is dropped and counted: the waiting
	/// edges are older and already owed.
	void push(uint32_t stamp) noexcept {
		const uint32_t received = received_.load(std::memory_order_relaxed);
		if (received - processed_.load(std::memory_order_acquire) >= kCapacity) {
			dropped_.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		stamps_[received & (kCapacity - 1)].store(stamp, std::memory_order_relaxed);
		received_.store(received + 1, std::memory_order_release);
	}

	/// The oldest waiting edge's stamp, if an edge is waiting.
	std::optional<uint32_t> pop() noexcept {
		const uint32_t processed = processed_.load(std::memory_order_relaxed);
		if (processed == received_.load(std::memory_order_acquire)) {
			return std::nullopt;
		}
		const uint32_t stamp = stamps_[processed & (kCapacity - 1)].load(std::memory_order_relaxed);
		processed_.store(processed + 1, std::memory_order_release);
		return stamp;
	}

	/// Edges dropped because the ring was full.
	[[nodiscard]] uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
	static_assert((kCapacity & (kCapacity - 1)) == 0);
	std::array<std::atomic<uint32_t>, kCapacity> stamps_{};
	std::atomic<uint32_t> received_{0};
	std::atomic<uint32_t> processed_{0};
	std::atomic<uint32_t> dropped_{0};
};

/// The edges the trigger-clock input has delivered.
extern TriggerClockEdges triggerClockEdges;

/// Point the BSP's trigger-clock handler at @ref triggerClockEdges.
void installTriggerClockInput() noexcept;

} // namespace deluge::playback
```

`src/deluge/playback/trigger_clock_input.cpp`:

```cpp
#include "playback/trigger_clock_input.h"

#include <libdeluge/cv_gate.h>

namespace deluge::playback {

TriggerClockEdges triggerClockEdges;

namespace {
/// The BSP calls this once per rising edge, on its trigger-clock task. `stamp` is the edge's capture stamp,
/// widened to 64 bits; it fits in 32.
extern "C" void onTriggerClockEdge(uint64_t stamp) noexcept {
	triggerClockEdges.push(static_cast<uint32_t>(stamp));
}
} // namespace

void installTriggerClockInput() noexcept {
	deluge_trigger_clock_set_handler(&onTriggerClockEdge);
}

} // namespace deluge::playback
```

Only the header is needed for the tests. `tests/unit/cpputest/CMakeLists.txt` compiles no source for it, and the
test finds the header through the `src/deluge` include path the other tests use.

- [ ] **Step 4: Run test to verify it passes**

Run: `./dbt test unit`
Expected: PASS.

- [ ] **Step 5: Drain the ring and install the handler**

In `src/deluge/playback/playback_handler.cpp`, delete the three `extern "C"` lines (122-124), add
`#include "playback/trigger_clock_input.h"` on its own line with the other includes, and replace the check at
146-151 with:

```cpp
	// Check analog clock input
	if (const auto stamp = deluge::playback::triggerClockEdges.pop()) {
		analogClockRisingEdge(*stamp);
	}
```

In `src/deluge/processing/engines/cv_engine.cpp`, add `#include "playback/trigger_clock_input.h"` on its own line,
and after `deluge_gate_init();`:

```cpp
	deluge::playback::installTriggerClockInput();
```

- Delete lines 50-56 of `src/bsp/rust/src/ffi_extra.rs` (the three trigger-clock statics and their comment), and
  keep `anythingInitiallyAttachedAsUSBHost`: P2 removes it.
- Delete `#define TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED 4` from `src/board_config.h`.
- Confirm nothing else names the deleted symbols:

Run: `grep -rn 'triggerClockRisingEdge\|TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED' src tests include | grep -v /target/`
Expected: no output.

- [ ] **Step 6: Give the Embassy host a trigger-clock input**

`cv_gate.rs` is device-only, so the host build has no `deluge_trigger_clock_set_handler`, and the call this task
adds would not link there. Add to `src/bsp/rust/src/host_link_stubs.rs`, in its `cv_gate.h` section:

```rust
/// The app's trigger-clock handler (0 = none), as `cv_gate.rs` keeps it on the device.
static TRIGGER_HANDLER: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn deluge_trigger_clock_set_handler(handler: crate::sys::DelugeTriggerClockHandler) {
    TRIGGER_HANDLER.store(handler.map_or(0, |f| f as usize), core::sync::atomic::Ordering::Relaxed);
}

/// Host harness only: deliver one rising edge to the installed handler, as the device's trigger-clock task does.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_trigger_clock_host_edge(stamp: u64) {
    let h = TRIGGER_HANDLER.load(core::sync::atomic::Ordering::Relaxed);
    if h != 0 {
        // SAFETY: h was stored from a valid DelugeTriggerClockHandler by the setter above.
        let f: unsafe extern "C" fn(u64) = unsafe { core::mem::transmute(h) };
        // SAFETY: the app's handler takes any stamp.
        unsafe { f(stamp) };
    }
}
```

Declare it in `include/libdeluge/cv_gate.h`, after `deluge_trigger_clock_set_handler`:

```c
#ifdef DELUGE_HOST
/// Host harness only: deliver one trigger-clock rising edge, stamped @p stamp, to the installed handler.
/// Defined only by the Rust BSP's `host_app` build; never in a device image.
void deluge_trigger_clock_host_edge(uint64_t stamp);
#endif
```

- [ ] **Step 7: Write the gate, and watch it fail**

`tests/integration/scenarios/clock_input_scenario.h`:

```cpp
/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

/// @brief Trigger-clock input reaches the transport: edges delivered through the BSP's handler, with analog
/// auto-start on, must start playback (TC-INPUT).
extern "C" {
void deluge_scenario_clock_input_begin(void);
bool deluge_scenario_clock_input_complete(void);
void deluge_scenario_clock_input_dispatch_step(void);
bool deluge_scenario_clock_input_step_done(void);
int32_t deluge_scenario_clock_input_failures(void);
} // extern "C"
```

`tests/integration/scenarios/clock_input_scenario.cpp`, with the same licence header:

```cpp
#include "scenarios/clock_input_scenario.h"

#include "libdeluge/cv_gate.h"
#include "playback/playback_handler.h"
#include "playback/trigger_clock_input.h"

#include <cstdio>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

namespace {

/// Edges to deliver. More than the ring holds, spaced so none is dropped.
constexpr int32_t kEdges = 8;
/// Ticks (virtual milliseconds) between edges: faster than the half-second auto-stop, slower than routine() runs.
constexpr int32_t kTicksBetweenEdges = 20;
/// Ticks after the last edge before the check, so routine() has drained the ring.
constexpr int32_t kSettleTicks = 10;

enum class Stage : uint8_t { Arm, Edges, Settle, Report, Done };

Stage g_stage = Stage::Arm;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;
int32_t g_edges = 0;

void fail(const char* what) {
	printf("TC-FAIL %s\n", what);
	g_failures++;
}

void runTick() {
	switch (g_stage) {
	case Stage::Arm:
		if (playbackHandler.playbackState != 0) {
			fail("playing before any edge");
		}
		playbackHandler.analogClockInputAutoStart = true;
		g_stage = Stage::Edges;
		break;

	case Stage::Edges:
		if (++g_ticks >= kTicksBetweenEdges) {
			g_ticks = 0;
			deluge_trigger_clock_host_edge(0);
			if (++g_edges == kEdges) {
				g_stage = Stage::Settle;
			}
		}
		break;

	case Stage::Settle:
		if (++g_ticks >= kSettleTicks) {
			g_stage = Stage::Report;
		}
		break;

	case Stage::Report: {
		const bool playing = playbackHandler.playbackState != 0;
		const uint32_t dropped = deluge::playback::triggerClockEdges.dropped();
		printf("TC-INPUT playing=%d edges=%d dropped=%u\n", playing ? 1 : 0, static_cast<int>(g_edges),
		       static_cast<unsigned>(dropped));
		if (!playing) {
			fail("trigger-clock edges with auto-start on did not start playback");
		}
		if (dropped != 0) {
			fail("edges spaced wider than routine() runs were dropped");
		}
		if (playing) {
			playbackHandler.endPlayback();
		}
		playbackHandler.analogClockInputAutoStart = false;
		printf("TC-RESULT failures=%d\n", static_cast<int>(g_failures));
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	}

	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_clock_input_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Arm;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	g_edges = 0;
}

bool deluge_scenario_clock_input_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_clock_input_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_clock_input_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_clock_input_failures(void) {
	return g_failures;
}

} // extern "C"
```

In `tests/integration/scenarios/scenario_table.cpp`:
- add `#include "scenarios/clock_input_scenario.h"` on its own line, in alphabetical order;
- add this entry to `kScenarios`, in alphabetical order (after `clear_song_undo`):

```cpp
    {"clock_input", nullptr, &deluge_scenario_clock_input_begin, &deluge_scenario_clock_input_complete,
     &deluge_scenario_clock_input_dispatch_step, &deluge_scenario_clock_input_step_done,
     &deluge_scenario_clock_input_failures},
```

In `tests/registry.toml`, after the `staging-release` entry:

```toml
[[harness]]
name = "clock-input"
path = "."
level = "integration"
runner = "scenario"
kind = "property"
status = "gate"
gates = "trigger-clock edges delivered through the BSP's handler, with analog auto-start on, must start playback (TC-INPUT), with none dropped when they arrive slower than PlaybackHandler::routine() runs. The input was dead while nothing installed the handler: the app polled globals that nothing wrote."
scenario = "clock_input"
markers = ["TC-INPUT"]
result = "TC-RESULT failures=0"
builds = ["golden-vt-render"]
requires = []
tier = "pr"
runtime = "~1m"
```

Check the new entry against the registry's schema: `./dbt test registry-selftest`. Expected: PASS.

Neutering check: comment out the `analogClockRisingEdge(*stamp);` line in `playback_handler.cpp`.
Run: `./dbt test clock-input`
Expected: FAIL, with `TC-FAIL trigger-clock edges with auto-start on did not start playback`. Restore the line.

- [ ] **Step 8: Run to verify it passes**

Run: `./dbt test clock-input unit bsp-host registry-selftest`
Expected: PASS.

- [ ] **Step 9: Device link check**

Run the device link check from Global Constraints.
Expected: exit 0.

- [ ] **Step 10: Commit**

```bash
git add src/deluge/playback/trigger_clock_input.h src/deluge/playback/trigger_clock_input.cpp \
  tests/unit/cpputest/trigger_clock_input_tests.cpp src/deluge/playback/playback_handler.cpp \
  src/deluge/processing/engines/cv_engine.cpp src/bsp/rust/src/ffi_extra.rs src/board_config.h \
  src/bsp/rust/src/host_link_stubs.rs include/libdeluge/cv_gate.h \
  tests/integration/scenarios/clock_input_scenario.h tests/integration/scenarios/clock_input_scenario.cpp \
  tests/integration/scenarios/scenario_table.cpp tests/registry.toml
git commit -m "fix: deliver trigger-clock edges to the app"
```

---

### Task 3: The write channel's answer slot

`file_write.rs` keys `ReadFile` and `WriteFile` answers by kind alone and ignores the path their collects pass. Its
answers carry no card generation, and the card flush skips the channel. An uncollected `Open` answer that `accept`
throws away leaks a handle from an 8-slot table. The slot moves into a pure module that fixes all four.

**Files:**
- Create: `src/bsp/rust/src/file_write_slot.rs`
- Create: `src/bsp/rust/tests/file_write_host.rs`
- Modify: `src/bsp/rust/src/main.rs`: `mod file_write_slot;` unconditionally, beside `mod file_write;` (line 250).
- Modify: `src/bsp/rust/src/file_write.rs`: statics, `accept`, `collect`, the two path collects, the task loop, and
  a new `flush_tables`.
- Modify: `src/bsp/rust/src/card.rs:50`: flush the write channel.
- Modify: `include/libdeluge/file_write.h`: the two path collects' docs (the path now selects the answer).

**Interfaces:**
- Produces:
  - `file_write_slot::{OpKind, OpKey, AnswerSlot, Take, WritePath, MAX_PATH_CHARS}`.
  - `OpKey::new(kind: OpKind, handle: u32, path: &str) -> Option<OpKey>`.
  - The `AnswerSlot` methods:
    - `new() -> Self` (const)
    - `publish(&mut self, key: OpKey, result: Result<u32, u32>, generation: u32)`
    - `take(&mut self, key: &OpKey, generation: u32) -> Take`
    - `discard(&mut self, key: &OpKey)`
    - `drain(&mut self)`
    - `take_orphan(&mut self) -> Option<u32>`
    - `has_orphans(&self) -> bool`
  - `Take::{Ready(u32), Failed(u32), Absent}`.
  - `file_write::flush_tables()`.

- [ ] **Step 1: Write the failing test**

`src/bsp/rust/tests/file_write_host.rs`:

```rust
//! The write channel's answer slot: one answer at a time, keyed by kind, handle and path, stamped with the card
//! generation it was asked under.
//!
//! `deluge-bsp-rust` is bin-only, so this recompiles `src/file_write_slot.rs` into the test binary via `#[path]`.
#![cfg(not(target_os = "none"))]

#[path = "../src/file_write_slot.rs"]
mod file_write_slot;

use file_write_slot::{AnswerSlot, MAX_PATH_CHARS, OpKey, OpKind, Take};

/// Nonzero on purpose: a publish that dropped its stamp would leave 0, and a slot tested at 0 would not notice.
const CARD: u32 = 3;

fn key(kind: OpKind, handle: u32, path: &str) -> OpKey {
    OpKey::new(kind, handle, path).expect("test paths fit")
}

#[test]
fn a_published_answer_is_taken_exactly_once() {
    let mut slot = AnswerSlot::new();
    let k = key(OpKind::WriteAt, 2, "");
    slot.publish(k.clone(), Ok(512), CARD);
    assert_eq!(slot.take(&k, CARD), Take::Ready(512));
    assert_eq!(slot.take(&k, CARD), Take::Absent);
}

#[test]
fn a_failure_carries_its_payload() {
    let mut slot = AnswerSlot::new();
    let k = key(OpKind::ReadFile, 0, "SONGS/A.XML");
    slot.publish(k.clone(), Err(40_000), CARD);
    assert_eq!(slot.take(&k, CARD), Take::Failed(40_000));
}

#[test]
fn a_wrong_path_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::ReadFile, 0, "SONGS/A.XML");
    slot.publish(mine.clone(), Ok(100), CARD);
    assert_eq!(slot.take(&key(OpKind::ReadFile, 0, "SONGS/B.XML"), CARD), Take::Absent);
    assert_eq!(slot.take(&mine, CARD), Take::Ready(100));
}

#[test]
fn a_wrong_kind_or_handle_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::WriteAt, 2, "");
    slot.publish(mine.clone(), Ok(1), CARD);
    assert_eq!(slot.take(&key(OpKind::ReadAt, 2, ""), CARD), Take::Absent);
    assert_eq!(slot.take(&key(OpKind::WriteAt, 3, ""), CARD), Take::Absent);
    assert_eq!(slot.take(&mine, CARD), Take::Ready(1));
}

#[test]
fn an_answer_from_an_earlier_card_is_removed_not_returned() {
    let mut slot = AnswerSlot::new();
    let k = key(OpKind::WriteFile, 0, "SYNTHS/P.XML");
    slot.publish(k.clone(), Ok(900), CARD);
    assert_eq!(slot.take(&k, CARD + 1), Take::Absent);
    assert!(slot.is_empty());
    assert_eq!(slot.take(&k, CARD), Take::Absent);
}

#[test]
fn a_discarded_open_answer_is_orphaned_for_closing() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(5), CARD);
    slot.discard(&open);
    assert!(slot.is_empty());
    assert_eq!(slot.take_orphan(), Some(5));
    assert_eq!(slot.take_orphan(), None);
}

#[test]
fn a_failed_open_leaves_no_orphan() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Err(0), CARD);
    slot.discard(&open);
    assert!(!slot.has_orphans());
}

#[test]
fn a_displaced_open_answer_is_orphaned() {
    let mut slot = AnswerSlot::new();
    slot.publish(key(OpKind::Open, 0, ""), Ok(6), CARD);
    slot.publish(key(OpKind::Stats, 0, ""), Ok(1000), CARD);
    assert_eq!(slot.take_orphan(), Some(6));
}

#[test]
fn a_stale_open_answer_is_orphaned() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(7), CARD);
    assert_eq!(slot.take(&open, CARD + 1), Take::Absent);
    assert_eq!(slot.take_orphan(), Some(7));
}

#[test]
fn a_flushed_open_answer_is_orphaned() {
    let mut slot = AnswerSlot::new();
    slot.publish(key(OpKind::Open, 0, ""), Ok(1), CARD);
    slot.drain();
    assert!(slot.is_empty());
    assert_eq!(slot.take_orphan(), Some(1));
}

#[test]
fn a_collected_open_answer_is_not_orphaned() {
    let mut slot = AnswerSlot::new();
    let open = key(OpKind::Open, 0, "");
    slot.publish(open.clone(), Ok(4), CARD);
    assert_eq!(slot.take(&open, CARD), Take::Ready(4));
    assert!(!slot.has_orphans());
}

#[test]
fn discarding_another_key_leaves_the_answer() {
    let mut slot = AnswerSlot::new();
    let mine = key(OpKind::ReadFile, 0, "A");
    slot.publish(mine.clone(), Ok(3), CARD);
    slot.discard(&key(OpKind::ReadFile, 0, "B"));
    assert_eq!(slot.take(&mine, CARD), Take::Ready(3));
}

#[test]
fn a_path_longer_than_the_bound_has_no_key() {
    let long = "x".repeat(MAX_PATH_CHARS + 1);
    assert!(OpKey::new(OpKind::ReadFile, 0, &long).is_none());
    assert!(OpKey::new(OpKind::ReadFile, 0, &long[..MAX_PATH_CHARS]).is_some());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./dbt test bsp-host`
Expected: FAIL. `file_write_host` does not compile: `src/file_write_slot.rs` is not found.

- [ ] **Step 3: Write the slot**

`src/bsp/rust/src/file_write_slot.rs`:

```rust
//! The write channel's answer slot.
//!
//! Pure, so `tests/file_write_host.rs` tests it without a card. The rules are `file_bytes`'s:
//! - a take with another operation's key leaves the answer for its owner;
//! - a take that finds its own answer stamped for an earlier card removes it and reports nothing.
//!
//! One rule is this channel's own. An `Open` answer carries a live stream handle, and the stream table has 8 slots.
//! Every successful `Open` answer that leaves the slot without being collected is kept as an orphan, and the
//! channel's task closes it.

/// Same bound as every other channel's path: 256 bytes with the NUL.
pub const MAX_PATH_CHARS: usize = 255;
pub type WritePath = heapless::String<MAX_PATH_CHARS>;

/// The stream table's size. No more handles than this can be open, so no more can be orphaned.
const MAX_ORPHANS: usize = 8;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum OpKind {
    Open,
    ReadAt,
    WriteAt,
    Truncate,
    Close,
    Stats,
    ReadFile,
    WriteFile,
}

/// Which operation an answer belongs to.
/// - Handle operations carry their handle and an empty path.
/// - `ReadFile` and `WriteFile` carry their path and handle 0.
/// - `Open` and `Stats` carry neither: the channel is single-flight, and their collects name no file.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct OpKey {
    pub kind: OpKind,
    pub handle: u32,
    pub path: WritePath,
}

impl OpKey {
    /// `None` when `path` is longer than [`MAX_PATH_CHARS`].
    pub fn new(kind: OpKind, handle: u32, path: &str) -> Option<Self> {
        let mut stored = WritePath::new();
        stored.push_str(path).ok()?;
        Some(Self {
            kind,
            handle,
            path: stored,
        })
    }
}

/// What a take found.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Take {
    /// This operation's answer: the handle, byte count or free-cluster count.
    Ready(u32),
    /// This operation failed. The payload is the size a whole-file read needed, or 1 when a whole-file write found
    /// the name taken; 0 otherwise.
    Failed(u32),
    /// No answer for this key and this card.
    Absent,
}

struct Answer {
    key: OpKey,
    result: Result<u32, u32>,
    generation: u32,
}

pub struct AnswerSlot {
    answer: Option<Answer>,
    orphans: heapless::Vec<u32, MAX_ORPHANS>,
}

impl AnswerSlot {
    pub const fn new() -> Self {
        Self {
            answer: None,
            orphans: heapless::Vec::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.answer.is_none()
    }

    /// Hold `key`'s answer, stamped with the card generation its request was made under. Whatever the slot held
    /// before is dropped.
    pub fn publish(&mut self, key: OpKey, result: Result<u32, u32>, generation: u32) {
        self.drain();
        self.answer = Some(Answer {
            key,
            result,
            generation,
        });
    }

    /// Take `key`'s answer if it is stamped `generation`. An answer for another key is left alone. An answer for
    /// this key stamped for another card is removed.
    pub fn take(&mut self, key: &OpKey, generation: u32) -> Take {
        if !self.answer.as_ref().is_some_and(|a| a.key == *key) {
            return Take::Absent;
        }
        let answer = self.answer.take().expect("checked just above");
        if answer.generation != generation {
            self.retire(answer);
            return Take::Absent;
        }
        match answer.result {
            Ok(value) => Take::Ready(value),
            Err(payload) => Take::Failed(payload),
        }
    }

    /// Drop the answer if it is `key`'s.
    pub fn discard(&mut self, key: &OpKey) {
        if self.answer.as_ref().is_some_and(|a| a.key == *key) {
            self.drain();
        }
    }

    /// Empty the slot.
    pub fn drain(&mut self) {
        if let Some(answer) = self.answer.take() {
            self.retire(answer);
        }
    }

    /// A handle an uncollected `Open` answer opened, for the task to close.
    pub fn take_orphan(&mut self) -> Option<u32> {
        self.orphans.pop()
    }

    pub fn has_orphans(&self) -> bool {
        !self.orphans.is_empty()
    }

    fn retire(&mut self, answer: Answer) {
        if let (OpKind::Open, Ok(handle)) = (answer.key.kind, answer.result) {
            let kept = self.orphans.push(handle);
            debug_assert!(kept.is_ok(), "more orphans than the stream table has handles");
        }
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./dbt test bsp-host`
Expected: PASS, with the 13 `file_write_host` tests listed.

- [ ] **Step 5: Put the slot under the channel**

In `src/bsp/rust/src/file_write.rs`, make these changes and leave the rest as it is.

**(a)** Delete `OpKind` (lines 99-112), `Answer` (114-125), `MAX_PATH_CHARS` and `WritePath` (127-132), and
`op_key` (173-185). Add:

```rust
use crate::file_write_slot::{AnswerSlot, OpKey, OpKind, Take, WritePath};

/// A request waiting for the task.
struct Queued {
    op: StreamOp,
    key: OpKey,
    /// The file an `Open`, `ReadFile` or `WriteFile` names; empty for the others.
    path: WritePath,
    /// The card generation the request was made under. Its answer is stamped with it, so an answer that finishes
    /// after an ejection is never collected as the new card's.
    generation: u32,
}

/// `op`'s key. `None` when a path-keyed operation's path does not fit.
fn op_key(op: &StreamOp, path: &str) -> Option<OpKey> {
    match *op {
        StreamOp::Open { .. } => OpKey::new(OpKind::Open, 0, ""),
        StreamOp::ReadAt { handle, .. } => OpKey::new(OpKind::ReadAt, handle, ""),
        StreamOp::WriteAt { handle, .. } => OpKey::new(OpKind::WriteAt, handle, ""),
        StreamOp::Truncate { handle, .. } => OpKey::new(OpKind::Truncate, handle, ""),
        StreamOp::Close { handle } => OpKey::new(OpKind::Close, handle, ""),
        StreamOp::Stats => OpKey::new(OpKind::Stats, 0, ""),
        StreamOp::ReadFile { .. } => OpKey::new(OpKind::ReadFile, 0, path),
        StreamOp::WriteFile { .. } => OpKey::new(OpKind::WriteFile, 0, path),
    }
}
```

**(b)** Change the three statics to:

```rust
static REQUEST: Mutex<CriticalSectionRawMutex, Option<Queued>> = Mutex::new(None);
/// The operation the task is running. Kept apart from [`REQUEST`] so a collect can tell "still running" (PENDING)
/// from "never asked" (UNKNOWN).
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<OpKey>> = Mutex::new(None);
static RESULT: Mutex<CriticalSectionRawMutex, AnswerSlot> = Mutex::new(AnswerSlot::new());
```

In `deluge_file_write_holds`, change `req.as_ref().is_some_and(|(op, _)| op_buffer(op) == addr)` to
`req.as_ref().is_some_and(|q| op_buffer(&q.op) == addr)`.

**(c)** Replace `accept`:

```rust
/// Accept an operation if nothing is in flight.
///
/// Drops an uncollected answer with the SAME key first. `STREAM_WRITE_CTX` recycles slot indices, so without
/// this, a recorder that inherits slot N would collect the previous recorder's answer as its own.
fn accept(op: StreamOp, path: &str) -> bool {
    let (Ok(mut req), Ok(in_flight), Ok(mut result)) =
        (REQUEST.try_lock(), IN_FLIGHT.try_lock(), RESULT.try_lock())
    else {
        return false;
    };
    // Single-flight. A second request while one is outstanding is REFUSED, never replaced: for the recorder a
    // dropped write is lost audio, so the caller must see the refusal.
    if req.is_some() || in_flight.is_some() {
        return false;
    }
    let (Some(key), Ok(stored)) = (op_key(&op, path), WritePath::try_from(path)) else {
        return false;
    };
    result.discard(&key);
    *req = Some(Queued {
        op,
        key,
        path: stored,
        generation: deluge_card_generation::current(),
    });
    WAKE.signal(());
    true
}
```

**(d)** Replace `collect`:

```rust
/// Collect `(kind, handle, path)`'s answer. `out` receives the `Ok` payload on READY, `out_needed` the `Err`
/// payload on FAILED.
///
/// # Safety
/// Either out-pointer may be null; when one is not, it must point at a writable `u32`.
unsafe fn collect(kind: OpKind, handle: u32, path: &str, out: *mut u32, out_needed: *mut u32) -> u8 {
    let Some(key) = OpKey::new(kind, handle, path) else {
        return DELUGE_FILE_WRITE_UNKNOWN;
    };
    if let Ok(mut result) = RESULT.try_lock() {
        let taken = result.take(&key, deluge_card_generation::current());
        if result.has_orphans() {
            WAKE.signal(());
        }
        match taken {
            Take::Ready(value) => {
                if !out.is_null() {
                    // SAFETY: non-null, and the caller guarantees it is writable.
                    unsafe { *out = value };
                }
                return DELUGE_FILE_WRITE_READY;
            }
            Take::Failed(payload) => {
                if !out_needed.is_null() {
                    // SAFETY: non-null, and the caller guarantees it is writable.
                    unsafe { *out_needed = payload };
                }
                return DELUGE_FILE_WRITE_FAILED;
            }
            Take::Absent => {}
        }
    }
    let queued = REQUEST
        .try_lock()
        .map(|r| r.as_ref().is_some_and(|q| q.key == key))
        .unwrap_or(false);
    let running = IN_FLIGHT
        .try_lock()
        .map(|f| f.as_ref() == Some(&key))
        .unwrap_or(false);
    if queued || running {
        DELUGE_FILE_WRITE_PENDING
    } else {
        // Nothing queued, nothing running, no answer for this card: ask again.
        DELUGE_FILE_WRITE_UNKNOWN
    }
}

/// A C path as `&str`; `None` for null or non-UTF-8.
///
/// # Safety
/// A non-null `path` must be NUL-terminated.
unsafe fn path_arg<'a>(path: *const c_char) -> Option<&'a str> {
    if path.is_null() {
        return None;
    }
    // SAFETY: caller guarantees NUL-termination.
    unsafe { CStr::from_ptr(path) }.to_str().ok()
}
```

**(e)** In every other collect, pass `""` as the new `path` argument: `collect(OpKind::WriteAt, handle, "",
out_written, core::ptr::null_mut())`, and the same for Open, ReadAt, Truncate, Close and Stats. Make the two path
collects use their path. Rename `_path` to `path` and replace the bodies:

```rust
pub unsafe extern "C" fn deluge_file_write_collect_read_file(
    path: *const c_char,
    out_len: *mut u32,
    out_needed: *mut u32,
) -> u8 {
    // SAFETY: forwarded contract.
    let Some(path) = (unsafe { path_arg(path) }) else {
        return DELUGE_FILE_WRITE_UNKNOWN;
    };
    // SAFETY: forwarded contract.
    unsafe { collect(OpKind::ReadFile, 0, path, out_len, out_needed) }
}
```

```rust
pub unsafe extern "C" fn deluge_file_write_collect_write_file(
    path: *const c_char,
    out_written: *mut u32,
    out_exists: *mut u8,
) -> u8 {
    // SAFETY: forwarded contract.
    let Some(path) = (unsafe { path_arg(path) }) else {
        return DELUGE_FILE_WRITE_UNKNOWN;
    };
    let mut reason: u32 = 0;
    // SAFETY: forwarded contract.
    let status = unsafe { collect(OpKind::WriteFile, 0, path, out_written, &raw mut reason) };
    if status == DELUGE_FILE_WRITE_FAILED && !out_exists.is_null() {
        // The error arm carries 1 for "the name was taken" and 0 for everything else.
        // SAFETY: non-null, and the caller guarantees it is writable.
        unsafe { *out_exists = u8::from(reason == 1) };
    }
    status
}
```

Replace their doc comments' "`_path` is unused…" paragraph with: "`path` selects the answer: a collect with
another file's path answers UNKNOWN and leaves this one's answer for its owner."

**(f)** In `file_write_task_body`, close orphans before each operation, take `Queued`, and publish before clearing
`IN_FLIGHT`, so no collect ever sees neither:

```rust
async fn close_orphans() {
    loop {
        let orphan = RESULT.lock().await.take_orphan();
        let Some(handle) = orphan else { break };
        backing::stream_close(handle).await;
    }
}

async fn file_write_task_body() {
    loop {
        WAKE.wait().await;
        loop {
            close_orphans().await;
            let Some(queued) = ({
                let mut req = REQUEST.lock().await;
                let taken = req.take();
                if let Some(q) = taken.as_ref() {
                    RUNNING_BUFFER.store(op_buffer(&q.op), core::sync::atomic::Ordering::Release);
                }
                taken
            }) else {
                break;
            };
            *IN_FLIGHT.lock().await = Some(queued.key.clone());
            // … the host latency block, unchanged …
            let path = queued.path.as_str();
            let result = match queued.op {
                // … the match arms, unchanged, reading `path` where they read `path.as_str()` …
            };
            RESULT.lock().await.publish(queued.key, result, queued.generation);
            *IN_FLIGHT.lock().await = None;
            RUNNING_BUFFER.store(0, core::sync::atomic::Ordering::Release);
        }
    }
}
```

The two `// …` lines above mark code this step keeps. They are not code to write: leave those lines of the
existing loop exactly as they are.

**(g)** Add the flush:

```rust
/// Forget everything asked of the card that is gone. Called by `deluge_card_flush`.
///
/// Drops a queued request (its buffer was never touched, so `deluge_file_write_holds` rightly stops naming it)
/// and a waiting answer. Leaves the running operation alone: it is still using its buffer, and its answer arrives
/// stamped for the old card, so no collect takes it. Each clear is `try_lock`ed and may not land; the generation
/// stamp is what makes that safe.
pub(crate) fn flush_tables() {
    if let Ok(mut request) = REQUEST.try_lock() {
        *request = None;
    }
    if let Ok(mut result) = RESULT.try_lock() {
        result.drain();
    }
    WAKE.signal(());
}
```

In `src/bsp/rust/src/card.rs`, after `super::file_bytes::flush_tables();` (line 50):

```rust
    super::file_write::flush_tables();
```

`card` is compiled for `any(target_os = "none", feature = "host_app")`. `file_write` is compiled unconditionally.
`flush_tables` has no `cfg`, so the call resolves in every carrier that has `card`.

**(h)** In `include/libdeluge/file_write.h`, change the docs of `deluge_file_write_collect_read_file` and
`deluge_file_write_collect_write_file` to say: "@param path The path the request named. It selects the answer: a
collect with another path answers @ref DELUGE_FILE_WRITE_UNKNOWN." Add to the header's top comment: "Answers are
stamped with the card generation their request was made under; after an ejection, an answer to a request from
before it is never collected, and its collect answers UNKNOWN."

- [ ] **Step 6: Run the channel's gates**

Run: `./dbt test bsp-host document-save-channel song-save-channel recorder-write save-roundtrip card-reinsert`
Expected: PASS. These are every gate that drives `file_write`, plus the ejection gate. Check the list:

Run: `grep -ln 'deluge_file_write\|io::Stream\|write_file' tests/integration/scenarios/*.cpp`

Add the registry entry of any listed scenario that is missing from the command above, then rerun.

- [ ] **Step 7: Device link check**

Run the device link check from Global Constraints.
Expected: exit 0.

- [ ] **Step 8: Commit**

```bash
git add src/bsp/rust/src/file_write_slot.rs src/bsp/rust/tests/file_write_host.rs src/bsp/rust/src/main.rs \
  src/bsp/rust/src/file_write.rs src/bsp/rust/src/card.rs include/libdeluge/file_write.h
git commit -m "fix: key the write channel's answers by path and card, and close orphaned opens"
```

---

### Task 4: `FileTimestamp` crosses the ABI as `DelugeTimestamp`

`deluge_file_bytes_request_set_time` takes a `FileTimestamp` by value. The C side passes a `DelugeTimestamp`
(`types.h`: `uint16_t year`, then five `uint8_t`). The Rust struct has no `#[repr(C)]`, so its layout is the
compiler's choice, and its doc claims a pin in `file_bytes_abi.cpp` that does not exist.

**Files:**
- Modify: `src/bsp/rust/src/file_bytes.rs:90-103`
- Modify: `src/deluge/storage/file_bytes_abi.cpp` (add the C-side layout asserts beside the existing ones)

**Interfaces:**
- Produces: `FileTimestamp` is `#[repr(C)]`: 8 bytes, align 2. Its field offsets are year 0, month 2, day 3,
  hour 4, minute 5 and second 6.

- [ ] **Step 1: Write the failing assert**

Add after `FileTimestamp` in `src/bsp/rust/src/file_bytes.rs`:

```rust
// `DelugeTimestamp`'s layout (types.h), which the C caller passes by value.
const _: () = {
    assert!(core::mem::size_of::<FileTimestamp>() == 8);
    assert!(core::mem::align_of::<FileTimestamp>() == 2);
    assert!(core::mem::offset_of!(FileTimestamp, year) == 0);
    assert!(core::mem::offset_of!(FileTimestamp, month) == 2);
    assert!(core::mem::offset_of!(FileTimestamp, day) == 3);
    assert!(core::mem::offset_of!(FileTimestamp, hour) == 4);
    assert!(core::mem::offset_of!(FileTimestamp, minute) == 5);
    assert!(core::mem::offset_of!(FileTimestamp, second) == 6);
};
```

Then, to prove the assert can fail, move `pub year: u16,` below `pub second: u8,` in the struct.

- [ ] **Step 2: Run to verify it fails**

Run: `./dbt test bsp-host`
Expected: FAIL. The `file_bytes_host` compile fails with "evaluation of constant value failed" at the
`offset_of!(FileTimestamp, year)` line. (If it passes, the assert is not wired: stop and find out why.)

- [ ] **Step 3: Fix the struct**

Move `year` back to first, and add `#[repr(C)]`:

```rust
/// `DelugeTimestamp` (types.h), hand-mirrored so this module compiles in carriers with no bindgen output. Passed
/// by value across the ABI, so it is `repr(C)`, and the asserts below pin it to the C layout.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct FileTimestamp {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
}
```

Add to `src/deluge/storage/file_bytes_abi.cpp`, beside the existing `static_assert`s:

```cpp
// The Rust byte channel's FileTimestamp mirrors this layout and asserts the same numbers.
static_assert(sizeof(DelugeTimestamp) == 8);
static_assert(alignof(DelugeTimestamp) == 2);
static_assert(offsetof(DelugeTimestamp, month) == 2);
static_assert(offsetof(DelugeTimestamp, second) == 6);
```

If `<cstddef>` is not already included there, add `#include <cstddef>` on its own line.

- [ ] **Step 4: Run to verify it passes**

Run: `./dbt test bsp-host settings-channel path-operations`
Expected: PASS. `path-operations` sets file times through this call. Check with
`grep -n set_time tests/integration/scenarios/*.cpp`, and add the entry of any other scenario that calls it.

- [ ] **Step 5: Device link check, then commit**

Run the device link check from Global Constraints.
Expected: exit 0.

```bash
git add src/bsp/rust/src/file_bytes.rs src/deluge/storage/file_bytes_abi.cpp
git commit -m "fix: give FileTimestamp the C layout it crosses the ABI with"
```

---

### Task 5: Whole-suite check and merge

- [ ] **Step 1:** Run: `./dbt test`
  Expected: every entry PASS, apart from entries whose registry status is `unavailable`.
- [ ] **Step 2:** Run the device link check.
  Expected: exit 0.
- [ ] **Step 3:** Merge into `next`: `git switch next && git merge --no-ff fix/libdeluge-abi-bugs`.
