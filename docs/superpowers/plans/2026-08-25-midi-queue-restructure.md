# MIDI Queue Manager Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the MIDI queue manager so it can be submitted as a single replacement PR: no duplicated transport code, no callback cycle into the engine, a host-testable drain path, and lane storage sized to what each lane actually needs.

**Architecture:** The CC-lane policy interaction (identity, coalescing, scheduled removal) becomes one templated implementation parameterised on a transport traits struct, replacing nine duplicated method pairs. The drain loops stay transport-specific because they are genuinely different algorithms, not one algorithm with different constants. Lane storage moves from five equal-capacity rings to a flat pool carved into per-lane views. The queue manager stops calling back into `MidiEngine`, which both removes a cycle and shrinks the test-double surface to three C functions.

**Tech Stack:** C++23, ARM GCC 14.2 (`./dbt build Debug`), CppUTest host unit tests (`tests/unit`, built with `cmake -S tests -B tests/build && ninja -C tests/build UnitTests`).

**Spec:** None. This plan is self-contained; the design rationale is stated per task. The subsystem's own design notes live in `docs/dev/systems/midi_queue_manager.md` and must be updated by Task 7.

## Global Constraints

- Firmware verification is `./dbt build Debug` only. Do not build Release.
- **Naming is `snake_case` for functions and variables.** The repo is migrating to it; the existing `snake_case` in `midi_queue_manager.*` is correct and must not be "fixed" to camelCase. `.clang-tidy` still says `ClassMethodCase: camelBack` and will emit false positives — ignore them, and do not change `.clang-tidy` in this PR.
- Lane capacities must remain exact powers of two. `MIDIQueueLane` masks positions rather than taking a modulo, and a `static_assert` enforces it.
- `write_pos` belongs to the producer, `read_pos` to the consumer. `consume_queued_messages()` runs in an ISR while senders enqueue from mainline. No task may introduce a consumer write to `write_pos`.
- Critical sections may cover only O(1) slot writes shared between producer and consumer — never a scan or a loop over lane contents.
- Do not change observable MIDI behaviour. Every task is a refactor; the existing 176 tests must keep passing unmodified except where a task explicitly says otherwise.
- Run the full unit suite (`./unit/UnitTests` from `tests/build`), not just the MIDI groups.
- Work on `midi-queue-manager-v6`, branched from the current tip (see Task 1, Step 1). Do not commit to `MIDI-Queue-Manager-v5`.
- Stage explicit paths in every commit. Never `git add -A`: `release-blockers.md` is untracked, unrelated, and must not be swept into a commit.

## File Structure

| File | Responsibility after this plan |
|---|---|
| `src/deluge/io/midi/midi_queue_definitions.h` | Priority enum, transport constants, per-lane capacity tables. |
| `src/deluge/io/midi/midi_queue_lane.h` | **New.** `MIDIQueueLane` ring view + `MIDIQueueStorage` pool. Transport-agnostic, no MIDI knowledge. |
| `src/deluge/io/midi/midi_cc_policy.h` | **New.** `MIDICCQueuePolicy`: debt, round-robin, single-pass selection. |
| `src/deluge/io/midi/midi_queue_transports.h` | **New.** `UsbTransport` / `DinTransport` traits structs. |
| `src/deluge/io/midi/midi_queue_manager.h` | `MIDIQueueManager` shared helpers + the two transport managers. Shrinks from 1136 lines. |
| `src/deluge/io/midi/midi_queue_manager.cpp` | Transport drain loops and pacing only. |
| `tests/unit/mocks/midi_transport_mock.{h,cpp}` | **New.** Link-time doubles for the UART/USB symbols, plus capture buffers tests assert on. |

---

### Task 1: Break the engine callback cycle

`MIDIQueueManagerUSB::enqueue_message` calls `midiEngine.flushUSBMIDIOutput()` at two points when the backlog grows (`midi_queue_manager.cpp:181` and `:193`). That makes the call graph a cycle — `MidiEngine → ConnectedUSBMIDIDevice → MIDIQueueManagerUSB → MidiEngine` — and means enqueueing a message can synchronously trigger the interrupt-masked drain.

It is also the only dependency that cannot be cheaply doubled at link time: doubling `midiEngine` drags in the whole `MidiEngine` class. Removing it first is what makes Task 2 affordable, so this task comes first.

The queue manager should *report* pressure and let the caller decide.

**Files:**
- Modify: `src/deluge/io/midi/midi_queue_manager.h` (`MIDIQueueManagerUSB::enqueue_message` declaration)
- Modify: `src/deluge/io/midi/midi_queue_manager.cpp` (`MIDIQueueManagerUSB::enqueue_message`, remove the `midi_engine.h` include if nothing else needs it)
- Modify: `src/deluge/io/midi/midi_device_manager.h`, `src/deluge/io/midi/midi_device_manager.cpp` (`ConnectedUSBMIDIDevice::enqueue_message`)
- Modify: `src/deluge/io/midi/midi_engine.cpp` (`sendUsbMidi`), `src/deluge/io/midi/cable_types/usb_common.cpp` (three call sites)
- Test: `tests/unit/midi_queue_manager_tests.cpp`

**Interfaces:**
- Produces: `[[nodiscard]] bool MIDIQueueManagerUSB::enqueue_message(uint32_t full_message, MIDIIntent intent)` — returns `true` when the caller should flush. `[[nodiscard]] bool ConnectedUSBMIDIDevice::enqueue_message(uint32_t fullMessage, MIDIIntent intent)` with the same meaning.

- [ ] **Step 1: Create the branch**

Branch from where the work already is, keeping every existing commit. No squash: the history stays
intact, so Sean's authorship on the original feature commit is preserved as-is rather than reconstructed
with a trailer.

```bash
cd /home/kate/GitHub/DelugeFirmware-clean
git checkout -b midi-queue-manager-v6
```

Confirm the starting point is sound before changing anything:

```bash
./dbt build Debug
cd tests/build && ./unit/UnitTests   # expect 176 tests, 0 failures
```

Note `release-blockers.md` is untracked and unrelated to this work. Leave it alone: stage explicit paths
in every commit below, never `git add -A`.

- [ ] **Step 2: Write the failing test**

Append to `tests/unit/midi_queue_manager_tests.cpp`:

```cpp
// --- Enqueue reports backpressure instead of flushing ---
//
// The queue manager must not call back into MidiEngine. Enqueue reports that a flush is wanted and the
// caller decides, which removes a cycle (engine -> device -> queue -> engine) and stops a mainline
// enqueue from synchronously triggering the interrupt-masked drain.

TEST_GROUP(MIDIQueueBackpressure){};

TEST(MIDIQueueBackpressure, EnqueueRequestsFlushOnlyOnceBacklogIsHigh) {
	MIDIQueueManagerUSB queue;
	queue.reset_queue_storage();

	// A single message is not backlog.
	CHECK_FALSE(queue.enqueue_message(0x09903C64, MIDIIntent::Event));

	// Push past k_usb_flush_backlog_message_threshold (16) and it should ask for a flush.
	bool asked = false;
	for (int i = 0; i < 32; i++) {
		asked = queue.enqueue_message(0x09903C64, MIDIIntent::Event) || asked;
	}
	CHECK_TRUE(asked);
}
```

- [ ] **Step 3: Run it to verify it fails**

```bash
cd tests/build && ninja UnitTests
```
Expected: FAIL to compile — `enqueue_message` currently returns `void`, so `CHECK_FALSE(...)` on it is a type error.

- [ ] **Step 4: Change the queue manager to report rather than act**

In `src/deluge/io/midi/midi_queue_manager.h`, change the declaration:

```cpp
	/// @brief Queues one packed USB-MIDI event, classifying it into the correct priority lane.
	/// @param full_message Packed USB-MIDI event.
	/// @param intent       Sender intent used to route Event vs. Continuous CCs into the correct lane.
	/// @return True when the queued backlog has grown past the flush threshold and the caller should
	///         flush. The queue manager deliberately does not flush itself: it is owned by the engine
	///         it would have to call, and a mainline enqueue must not trigger the interrupt-masked drain.
	[[nodiscard]] bool enqueue_message(uint32_t full_message, MIDIIntent intent);
```

In `src/deluge/io/midi/midi_queue_manager.cpp`, replace the body's two `midiEngine.flushUSBMIDIOutput()` blocks. The function becomes:

```cpp
bool MIDIQueueManagerUSB::enqueue_message(uint32_t full_message, MIDIIntent intent) {
	// Total messages currently queued across all priority lanes for this device.
	uint32_t queued = queue_manager_.total_queued_messages();
	bool wants_flush = queued > k_usb_flush_backlog_message_threshold;

	QueuePriority priority = classify_packed_usb_priority(full_message, intent);
	uint16_t queue_size = queue_manager_.queue_count(static_cast<uint8_t>(priority));
	if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
		// The lane is full. Ask the caller to flush; it will re-enqueue nothing, so this message is
		// dropped rather than overwriting unread queued data.
		// TODO: show some error message
		return true;
	}

	bool queued_ok = enqueue_message_with_cc_policy(priority, full_message);
	if (queued_ok) {
		anythingInUSBOutputBuffer = true;
	}
	return wants_flush;
}
```

Note this drops the old "flush, then re-check, then maybe queue anyway" retry. That retry only worked because enqueue could flush synchronously; with the cycle broken the caller flushes and the next message gets the freed space. Removing it is the point of the task.

Delete `#include "io/midi/midi_engine.h"` from `midi_queue_manager.cpp` if no other reference to `midiEngine` remains — check with `grep -n "midiEngine" src/deluge/io/midi/midi_queue_manager.cpp`.

- [ ] **Step 5: Propagate through the device wrapper**

`src/deluge/io/midi/midi_device_manager.h`:

```cpp
	/// @brief Classifies, optionally coalesces, and enqueues one outgoing MIDI message into USB priority lanes.
	/// @param fullMessage Packed USB-MIDI event.
	/// @param intent      Sender intent used to route Event vs. Continuous CCs into the correct lane.
	/// @return True when the caller should flush USB output.
	[[nodiscard]] bool enqueue_message(uint32_t fullMessage, MIDIIntent intent);
```

`src/deluge/io/midi/midi_device_manager.cpp`:

```cpp
bool ConnectedUSBMIDIDevice::enqueue_message(uint32_t fullMessage, MIDIIntent intent) {
	return queue_manager().enqueue_message(fullMessage, intent);
}
```

- [ ] **Step 6: Act on the result at the call sites**

In `src/deluge/io/midi/midi_engine.cpp`, `sendUsbMidi` — replace the bare call:

```cpp
						if (connectedDevice->enqueue_message(usb_cable_message, message.intent)
						    && anyUSBSendingStillHappening[0] == 0) {
							flushUSBMIDIOutput();
						}
```

In `src/deluge/io/midi/cable_types/usb_common.cpp`, all three call sites take the same shape. For `sendMessage` (line ~53):

```cpp
				if (connectedDevice->enqueue_message(usb_cable_message, message.intent)
				    && anyUSBSendingStillHappening[0] == 0) {
					midiEngine.flushUSBMIDIOutput();
				}
```

For the two SysEx sites (lines ~112 and ~137), the value is not actionable mid-chunk — a SysEx stream must finish queueing before any flush — so discard it explicitly and flush once after the loop:

```cpp
		(void)connectedDevice->enqueue_message(packed, MIDIIntent::Event);
```

- [ ] **Step 7: Run the tests**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: PASS, 177 tests.

- [ ] **Step 8: Build the firmware**

```bash
./dbt build Debug
```
Expected: links with no errors.

- [ ] **Step 9: Commit**

```bash
git add src/deluge/io/midi/ src/deluge/model/ tests/unit/midi_queue_manager_tests.cpp
git commit -m "report USB queue backpressure instead of flushing from enqueue

The queue manager called midiEngine.flushUSBMIDIOutput() from inside enqueue_message, so the call graph
was a cycle - engine to device to queue to engine - and a mainline enqueue could synchronously trigger
the interrupt-masked drain. Enqueue now returns whether a flush is wanted and the caller decides.

This also removes the last dependency that cannot be doubled at link time, which is what makes the
transport drain path host-testable."
```

---

### Task 2: Link-time test doubles for the transport path

After Task 1, `midi_queue_manager.cpp` depends on exactly three external symbols: `uartGetTxBufferSpace`, `bufferMIDIUart`, and the `anyUSBSendingStillHappening` array. Providing those in a test-only translation unit puts the whole DIN drain path — pacing, allowance, SysEx locking, ordering — under host test with no production code change.

This is the highest-leverage task in the plan. Three of the four serious defects found in review lived in the drain path and were invisible to inspection.

**Files:**
- Create: `tests/unit/mocks/midi_transport_mock.h`, `tests/unit/mocks/midi_transport_mock.cpp`
- Modify: `tests/unit/CMakeLists.txt`
- Test: `tests/unit/midi_din_drain_tests.cpp` (new)

**Interfaces:**
- Consumes: `MIDIQueueManagerDIN::enqueue_message(MIDIMessage)`, `::enqueue_sysex(uint8_t const*, int32_t)`, `::consume_queued_messages(uint32_t now_sample_timer)`, `::reset_serial_state(uint32_t)` from the existing header.
- Produces: `MidiTransportMock::reset()`, `::sent_bytes()` returning `std::vector<uint8_t> const&`, `::set_uart_space(int32_t)`.

- [ ] **Step 1: Write the mock**

`tests/unit/mocks/midi_transport_mock.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

/// Link-time double for the UART and USB symbols the MIDI queue manager writes through.
///
/// The queue manager's drain path talks to the hardware via three free symbols. Defining them here
/// rather than injecting an interface keeps the production code untouched and costs no indirection on
/// the real-time path; the seam is the linker.
namespace MidiTransportMock {
/// Clears captured bytes and restores default UART space.
void reset();
/// Every byte the queue manager handed to bufferMIDIUart(), in order.
std::vector<uint8_t> const& sent_bytes();
/// Sets the space uartGetTxBufferSpace() will report, to exercise the pacing paths.
void set_uart_space(int32_t space);
} // namespace MidiTransportMock
```

`tests/unit/mocks/midi_transport_mock.cpp`:

```cpp
#include "midi_transport_mock.h"

namespace {
std::vector<uint8_t> g_sent;
int32_t g_uart_space = 1024;
} // namespace

namespace MidiTransportMock {
void reset() {
	g_sent.clear();
	g_uart_space = 1024;
}
std::vector<uint8_t> const& sent_bytes() {
	return g_sent;
}
void set_uart_space(int32_t space) {
	g_uart_space = space;
}
} // namespace MidiTransportMock

extern "C" {
/// Captures a byte the queue manager staged for the DIN port.
void bufferMIDIUart(uint8_t byte) {
	g_sent.push_back(byte);
	if (g_uart_space > 0) {
		g_uart_space--;
	}
}
int32_t uartGetTxBufferSpace(int32_t item) {
	(void)item;
	return g_uart_space;
}
/// The real firmware sets this from the USB driver; tests keep it at "idle".
uint8_t anyUSBSendingStillHappening[2] = {0, 0};
}
```

Check the exact signatures before writing: `grep -n "uartGetTxBufferSpace\|bufferMIDIUart" src/RZA1/uart/sio_char.h src/drivers/uart/uart.h`. If they differ from the above, match the real declarations exactly — a mismatch is a link error, not a silent bug.

- [ ] **Step 2: Add the drain source and mock to the test build**

In `tests/unit/CMakeLists.txt`, add to the `deluge_SOURCES` glob list:

```cmake
        # For MIDI queue manager drain tests
        ../../src/deluge/io/midi/midi_queue_manager.cpp
        ../../src/deluge/model/midi/message.cpp
```

and add to the `add_executable(UnitTests ...)` list:

```cmake
        midi_din_drain_tests.cpp
```

The mock is picked up by the existing `mocks/*` glob.

- [ ] **Step 3: Write the first drain test**

`tests/unit/midi_din_drain_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue_manager.h"
#include "mocks/midi_transport_mock.h"

// Drives the real DIN drain path against a captured UART. This is the path where the ordering and
// concurrency defects lived, and it is only reachable on the host because the three UART/USB symbols
// are provided by a link-time double.

TEST_GROUP(MIDIDinDrain) {
	MIDIQueueManagerDIN queue;
	void setup() override {
		MidiTransportMock::reset();
		queue.reset_queue_storage();
		queue.reset_serial_state(0);
	}
};

TEST(MIDIDinDrain, ClockOvertakesQueuedCC) {
	// A CC queued first must not delay a clock queued after it: that is the whole point of the lanes.
	MIDIMessage cc = MIDIMessage::cc(0, 20, 64);
	cc.intent = MIDIIntent::Continuous;
	queue.enqueue_message(cc);
	queue.enqueue_message(MIDIMessage{.statusType = 0x0F, .channel = 0x08, .data1 = 0, .data2 = 0});

	queue.consume_queued_messages(48000);

	auto const& sent = MidiTransportMock::sent_bytes();
	CHECK(sent.size() >= 1);
	CHECK_EQUAL(0xF8, sent[0]); // clock first, despite being queued second
}

TEST(MIDIDinDrain, EventCCsKeepTheirOrderAndTheirDuplicateValues) {
	// The RPN case, end to end: five CCs, two of which repeat a CC number with a different value.
	uint8_t const ccs[][2] = {{100, 6}, {101, 0}, {6, 4}, {100, 127}, {101, 127}};
	for (auto const& c : ccs) {
		queue.enqueue_message(MIDIMessage::cc(0, c[0], c[1])); // default Event intent
	}

	for (int i = 0; i < 8; i++) {
		queue.consume_queued_messages(48000 + i * 48000);
	}

	auto const& sent = MidiTransportMock::sent_bytes();
	CHECK_EQUAL(15, sent.size()); // five complete 3-byte CC messages, none merged
	CHECK_EQUAL(100, sent[1]);
	CHECK_EQUAL(6, sent[2]);
	CHECK_EQUAL(101, sent[4]);
	CHECK_EQUAL(0, sent[5]);
	CHECK_EQUAL(6, sent[7]);
	CHECK_EQUAL(100, sent[10]);
	CHECK_EQUAL(127, sent[11]);
	CHECK_EQUAL(101, sent[13]);
	CHECK_EQUAL(127, sent[14]);
}

TEST(MIDIDinDrain, SysExIsNotInterleaved) {
	uint8_t const sysex[] = {0xF0, 0x7D, 0x01, 0x02, 0xF7};
	CHECK_TRUE(queue.enqueue_sysex(sysex, sizeof(sysex)));
	MIDIMessage cc = MIDIMessage::cc(0, 20, 64);
	cc.intent = MIDIIntent::Continuous;
	queue.enqueue_message(cc);

	for (int i = 0; i < 8; i++) {
		queue.consume_queued_messages(48000 + i * 48000);
	}

	// Once the stream starts, every byte up to 0xF7 must be SysEx.
	auto const& sent = MidiTransportMock::sent_bytes();
	size_t start = 0;
	while (start < sent.size() && sent[start] != 0xF0) {
		start++;
	}
	CHECK(start < sent.size());
	for (size_t i = start; i < start + sizeof(sysex); i++) {
		CHECK(sent[i] == sysex[i - start]);
	}
}
```

- [ ] **Step 4: Run them**

```bash
cd /home/kate/GitHub/DelugeFirmware-clean
cmake -S tests -B tests/build          # re-run: CMakeLists.txt changed
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: PASS. If `midi_queue_manager.cpp` fails to link, the missing symbol names it needs are listed by the linker — add each to the mock rather than stubbing it inside production code.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/mocks/midi_transport_mock.h tests/unit/mocks/midi_transport_mock.cpp \
        tests/unit/midi_din_drain_tests.cpp tests/unit/CMakeLists.txt
git commit -m "put the DIN drain path under host test via link-time doubles

The drain path depends on three free symbols (uartGetTxBufferSpace, bufferMIDIUart,
anyUSBSendingStillHappening). Providing them in a test-only translation unit reaches the whole pacing,
priority and SysEx-locking path with no production code change and no indirection on the real-time path.

This is where the ordering and concurrency defects found in review lived; none of them were reachable by
the existing header-only tests."
```

---

### Task 3: Per-lane capacities

Five lanes at a uniform 1024 entries costs 121KB for USB (`usbQueueManagers`) plus 10KB for DIN — measured from the linked ELF — against roughly 24KB for the single ring this subsystem replaced. The clock lane will never hold 1024 realtime bytes.

`MIDIQueueStorage` currently holds `std::array<MIDIQueueLane<T, Capacity>, LaneCount>`, so every lane is the same type and therefore the same size. Giving lanes different capacities means the capacity can no longer be a template parameter of the lane. Move the storage to one flat pool carved into views, with the mask as a member instead of a compile-time constant.

Sizing rationale: the SysEx lane must hold one complete stream all-or-nothing, and the largest the firmware stages is `MidiEngine::sysex_fmt_buffer[1024]`. The CC lane must hold one entry per distinct pending CC identity. Everything else is small.

**Files:**
- Create: `src/deluge/io/midi/midi_queue_lane.h` (moved out of `midi_queue_manager.h`)
- Modify: `src/deluge/io/midi/midi_queue_definitions.h` (capacity tables)
- Modify: `src/deluge/io/midi/midi_queue_manager.h` (include the new header; storage member changes)
- Test: `tests/unit/midi_queue_manager_tests.cpp`

**Interfaces:**
- Produces: `template <typename T> class MIDIQueueLane` with `T* data; uint16_t mask; uint16_t read_pos; uint16_t write_pos;` and the existing method set (`push`, `pop`, `pop_many`, `peek`, `size`, `space`, `empty`, `clear`, `overwrite_at`, `remove_span_via_head_swap`). `MIDIQueueStorage<T, LaneCount, Capacities>::lane_capacity(uint8_t lane)` returning `uint16_t`, where `Capacities` is a reference to an `inline constexpr uint16_t[LaneCount]` table.

- [ ] **Step 1: Add the capacity tables**

In `src/deluge/io/midi/midi_queue_definitions.h`, after the `QueuePriority` enum:

```cpp
/// Per-lane ring capacities, indexed by QueuePriority.
///
/// Declared `inline constexpr` deliberately: MIDIQueueStorage takes these by reference as a non-type
/// template parameter, which requires external linkage. A plain `constexpr` array in a header has
/// internal linkage, so every translation unit would get a distinct entity and the instantiations would
/// not match.
///
/// Each MUST be an exact power of two: MIDIQueueLane masks positions rather than taking a modulo.
/// Sized to what each lane actually holds rather than uniformly, because five equal lanes cost about
/// five times what this subsystem needs.
///
/// SysEx is the largest because a stream is queued all-or-nothing and the biggest the firmware stages is
/// MidiEngine::sysex_fmt_buffer[1024]. CC is next because it holds one entry per distinct pending CC
/// identity. Clock carries single realtime bytes and never backs up.
inline constexpr uint16_t k_usb_lane_capacity[QUEUE_PRIORITY_COUNT] = {
    32,  // QUEUE_PRIORITY_CLOCK: single-event realtime messages
    128, // QUEUE_PRIORITY_NOTES
    128, // QUEUE_PRIORITY_EXPRESSION: also carries Event CCs and program changes
    256, // QUEUE_PRIORITY_CC: one entry per distinct Continuous CC identity
    512, // QUEUE_PRIORITY_SYSEX: 1024 bytes at up to 3 payload bytes per event
};

inline constexpr uint16_t k_din_lane_capacity[QUEUE_PRIORITY_COUNT] = {
    32,   // QUEUE_PRIORITY_CLOCK: one byte per realtime message
    256,  // QUEUE_PRIORITY_NOTES: 3 bytes per message
    256,  // QUEUE_PRIORITY_EXPRESSION
    512,  // QUEUE_PRIORITY_CC: 3 bytes per message
    2048, // QUEUE_PRIORITY_SYSEX: one complete 1024-byte stream plus headroom
};
```

- [ ] **Step 2: Write the failing test**

Append to `tests/unit/midi_queue_manager_tests.cpp`:

```cpp
// --- Per-lane capacity ---

TEST_GROUP(MIDIQueueLaneCapacity){};

TEST(MIDIQueueLaneCapacity, EveryCapacityIsAPowerOfTwo) {
	// The ring masks positions instead of taking a modulo, so this is a correctness requirement.
	for (int i = 0; i < QUEUE_PRIORITY_COUNT; i++) {
		uint16_t usb = k_usb_lane_capacity[i];
		uint16_t din = k_din_lane_capacity[i];
		CHECK(usb != 0 && (usb & (usb - 1)) == 0);
		CHECK(din != 0 && (din & (din - 1)) == 0);
	}
}

TEST(MIDIQueueLaneCapacity, SysExLaneHoldsACompleteMaximumStream) {
	// enqueue_sysex is all-or-nothing, so a lane that cannot hold the largest stream the firmware
	// stages would silently drop it. One slot is always reserved, hence the strict comparison.
	CHECK(k_din_lane_capacity[QUEUE_PRIORITY_SYSEX] > 1024);
}
```

- [ ] **Step 3: Run it to verify it fails**

```bash
cd tests/build && ninja UnitTests
```
Expected: FAIL to compile — `k_usb_lane_capacity` is not declared until Step 1 is in place and the header is included. If Step 1 is already done, this passes immediately; that is fine, it is a guard on the table rather than on new behaviour.

- [ ] **Step 4: Move the lane into its own header and make capacity a member**

Create `src/deluge/io/midi/midi_queue_lane.h`. Move `MIDIQueueLane` and `MIDIQueueStorage` verbatim out of `midi_queue_manager.h`, then change the lane from a capacity template to a view:

```cpp
/// @brief Power-of-two ring buffer view over storage owned by MIDIQueueStorage.
///
/// @warning `mask` must be `capacity - 1` for a power-of-two capacity: positions wrap with a bitmask,
///          not a modulo by an arbitrary size.
/// @note The lane always keeps one slot unused so `read_pos == write_pos` can unambiguously mean empty.
///       Usable capacity is therefore `capacity - 1`. All logical offsets are relative to `read_pos`.
template <typename T>
class MIDIQueueLane {
public:
	T* data{nullptr};
	uint16_t mask{0};
	uint16_t read_pos{0};
	uint16_t write_pos{0};

	[[nodiscard]] uint16_t capacity() const { return static_cast<uint16_t>(mask + 1); }
	[[nodiscard]] bool empty() const { return read_pos == write_pos; }
	[[nodiscard]] uint16_t size() const { return static_cast<uint16_t>((write_pos - read_pos) & mask); }
	[[nodiscard]] uint16_t space() const { return static_cast<uint16_t>(mask - size()); }
	[[nodiscard]] T peek(uint16_t offset = 0) const { return data[(read_pos + offset) & mask]; }
	// ... every other method as before, with `& (Capacity - 1)` replaced by `& mask`
};
```

`MIDIQueueStorage` owns the pool and hands each lane its slice:

```cpp
/// @brief Fixed set of priority lanes carved from one flat pool, each with its own capacity.
template <typename T, size_t LaneCount, uint16_t const (&Capacities)[LaneCount]>
class MIDIQueueStorage {
public:
	MIDIQueueStorage() {
		uint32_t offset = 0;
		for (size_t i = 0; i < LaneCount; i++) {
			lanes[i].data = pool.data() + offset;
			lanes[i].mask = static_cast<uint16_t>(Capacities[i] - 1);
			offset += Capacities[i];
		}
	}

	[[nodiscard]] uint16_t lane_capacity(uint8_t lane) const { return lanes[lane].capacity(); }

	std::array<MIDIQueueLane<T>, LaneCount> lanes{};

	// ... existing accessors unchanged

private:
	static constexpr uint32_t total_capacity() {
		uint32_t total = 0;
		for (size_t i = 0; i < LaneCount; i++) {
			total += Capacities[i];
		}
		return total;
	}
	std::array<T, total_capacity()> pool{};
};
```

Then in `midi_queue_manager.h`, the two managers declare their storage as
`MIDIQueueManagerDeviceState<uint32_t, QUEUE_PRIORITY_COUNT, k_usb_lane_capacity>` and
`MIDIQueueManagerDeviceState<uint8_t, QUEUE_PRIORITY_COUNT, k_din_lane_capacity>` respectively, threading the
`Capacities` reference parameter through. Every `MIDI_SEND_BUFFER_LEN_RING - 1` and `k_serial_queue_capacity`
bound in `midi_queue_manager.cpp` becomes `queue_manager_.lane_capacity(lane) - 1`.

Find them with: `grep -n "MIDI_SEND_BUFFER_LEN_RING\|k_serial_queue_capacity" src/deluge/io/midi/midi_queue_manager.cpp src/deluge/io/midi/midi_queue_manager.h`

- [ ] **Step 5: Run the full suite**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: PASS. The existing lane tests construct `MIDIQueueLane<uint32_t, 8>` and `MIDIQueueLane<uint8_t, 16>`; they must be updated to the view form. Give each a backing array:

```cpp
	uint32_t backing[8]{};
	MIDIQueueLane<uint32_t> lane{.data = backing, .mask = 7};
```

- [ ] **Step 6: Build and measure the saving**

```bash
./dbt build Debug
toolchain/v22/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-nm --print-size --radix=d \
  build/Debug/deluge.elf | grep -iE "usbQueueManagers|connectedDINMIDIDevice"
```
Expected: `usbQueueManagers` around 25KB (was 123912 bytes) and `connectedDINMIDIDevice` around 3KB (was 10420). Record the actual numbers in the commit message; if they are far off, the capacity tables did not take effect.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/io/midi/ tests/unit/midi_queue_manager_tests.cpp
git commit -m "size each MIDI queue lane to what it holds

Five lanes at a uniform 1024 entries cost 121KB for USB and 10KB for DIN, against about 24KB for the
single ring this subsystem replaced. The clock lane never holds 1024 realtime bytes.

Lanes become views over one flat pool with per-lane capacity, so the mask moves from a template
parameter to a member. SysEx stays largest because a stream is queued all-or-nothing and the biggest the
firmware stages is 1024 bytes."
```

---

### Task 4: Transport traits, replacing the duplicated method pairs

Nine method pairs exist once for USB and once for DIN — `enqueue_message_with_cc_policy`, `coalesce_cc_message`, `handle_cc_lane`, `pop_next_scheduled_cc_message`, `begin_cc_message_scan`, `next_cc_message`, `remove_cc_message_at`, `pop_lane`, `reset_queue_storage` — each a near-identical lambda adapter over the shared policy. This is the maintainer's standing feedback from PR #4765 and the largest single simplification available.

The transports genuinely differ in only four ways: element type, how identity is read out of an element, how many elements one message spans, and how a value byte is rewritten. That is a traits shape.

**Scope note:** the *drain loops* stay separate. USB builds a fixed-size transfer buffer against a message-count allowance; DIN paces bytes against a Q8 allowance and UART space and validates variable-length heads. Those are different algorithms, not one algorithm with different constants, and forcing them together would be worse than the duplication. This task consolidates the CC-policy interaction only — which is where all nine duplicated pairs live.

**Files:**
- Create: `src/deluge/io/midi/midi_queue_transports.h`
- Modify: `src/deluge/io/midi/midi_queue_manager.h`, `src/deluge/io/midi/midi_queue_manager.cpp`
- Test: `tests/unit/midi_queue_manager_tests.cpp`

**Interfaces:**
- Produces: `UsbTransport` and `DinTransport` traits structs, and `template <typename Transport> class MIDICCLanePolicy` providing `coalesce`, `pop_scheduled`, and `enqueue_with_cc_policy`.

- [ ] **Step 1: Define the traits**

Create `src/deluge/io/midi/midi_queue_transports.h`:

```cpp
#pragma once
#include "io/midi/midi_queue_definitions.h"
#include "model/midi/message.h"
#include <cstdint>

/// @brief Transport traits for USB: one packed USB-MIDI event per queue element.
///
/// The CC lane policy is identical for both transports apart from these four operations, so it is
/// written once against this interface rather than duplicated per transport.
struct UsbTransport {
	using Element = uint32_t;

	/// @brief Elements one queued channel-CC message occupies. One packed event holds a whole message.
	static constexpr uint16_t cc_span = 1;

	/// @brief True when this element is a channel CC and therefore a coalescing/scheduling candidate.
	static bool is_channel_cc(Element const* e) {
		uint8_t status = static_cast<uint8_t>((e[0] >> 8) & 0xFF);
		return MIDIQueueManager::is_channel_cc_status_byte(status);
	}
	/// @brief MIDI status byte (type and channel) of a channel-CC element.
	static uint8_t status(Element const* e) { return static_cast<uint8_t>((e[0] >> 8) & 0xFF); }
	/// @brief CC number of a channel-CC element.
	static uint8_t cc_number(Element const* e) { return static_cast<uint8_t>((e[0] >> 16) & 0xFF); }
	/// @brief Rewrites only the value byte, preserving cable/CIN, status and CC number.
	static void set_value(Element* e, uint8_t value) {
		e[0] = (e[0] & 0x00FFFFFFu) | (static_cast<uint32_t>(value) << 24);
	}
};

/// @brief Transport traits for DIN: one raw serial byte per queue element.
struct DinTransport {
	using Element = uint8_t;

	/// @brief Elements one queued channel-CC message occupies: status, CC number, value.
	static constexpr uint16_t cc_span = MIDIQueueManager::k_channel_cc_message_length;

	static bool is_channel_cc(Element const* e) { return MIDIQueueManager::is_channel_cc_status_byte(e[0]); }
	static uint8_t status(Element const* e) { return e[0]; }
	static uint8_t cc_number(Element const* e) { return e[1]; }
	static void set_value(Element* e, uint8_t value) { e[2] = value; }
};
```

- [ ] **Step 2: Write the failing test**

Append to `tests/unit/midi_queue_manager_tests.cpp`:

```cpp
// --- Transport traits ---
//
// Both transports are driven through the same CC-lane policy; these pin the four operations that
// actually differ, so a mistake in either traits struct fails here rather than as scrambled MIDI.

TEST_GROUP(MIDITransportTraits){};

TEST(MIDITransportTraits, UsbTraitsReadAndRewriteAPackedEvent) {
	// byte0 cable/CIN, byte1 status, byte2 CC number, byte3 value
	uint32_t e = (uint32_t{64} << 24) | (uint32_t{74} << 16) | (uint32_t{0xB0} << 8) | 0x0B;
	CHECK_TRUE(UsbTransport::is_channel_cc(&e));
	CHECK_EQUAL(0xB0, UsbTransport::status(&e));
	CHECK_EQUAL(74, UsbTransport::cc_number(&e));

	UsbTransport::set_value(&e, 127);
	CHECK_EQUAL(127, static_cast<uint8_t>(e >> 24));
	CHECK_EQUAL(0xB0, UsbTransport::status(&e)); // identity untouched
	CHECK_EQUAL(74, UsbTransport::cc_number(&e));
	CHECK_EQUAL(0x0B, static_cast<uint8_t>(e & 0xFF)); // cable/CIN untouched
}

TEST(MIDITransportTraits, DinTraitsReadAndRewriteAThreeByteMessage) {
	uint8_t m[3] = {0xB0, 74, 64};
	CHECK_TRUE(DinTransport::is_channel_cc(m));
	CHECK_EQUAL(0xB0, DinTransport::status(m));
	CHECK_EQUAL(74, DinTransport::cc_number(m));

	DinTransport::set_value(m, 127);
	CHECK_EQUAL(127, m[2]);
	CHECK_EQUAL(0xB0, m[0]);
	CHECK_EQUAL(74, m[1]);
}

TEST(MIDITransportTraits, SpansMatchTheStorageUnit) {
	CHECK_EQUAL(1, UsbTransport::cc_span);
	CHECK_EQUAL(3, DinTransport::cc_span);
}
```

- [ ] **Step 3: Run it to verify it fails**

```bash
cd tests/build && ninja UnitTests
```
Expected: FAIL — `UsbTransport` not declared until the header is included in the test file. Add `#include "io/midi/midi_queue_transports.h"`.

- [ ] **Step 4: Write the shared CC lane policy**

Add to `src/deluge/io/midi/midi_cc_policy.h` (created in this task by moving `MIDICCQueuePolicy` out of `midi_queue_manager.h`):

```cpp
/// @brief The CC-lane half of the queue policy, written once for both transports.
///
/// Replaces nine method pairs that previously existed once per transport as near-identical lambda
/// adapters. Everything that genuinely differs between USB and DIN is in the Transport traits.
template <typename Transport>
class MIDICCLanePolicy {
public:
	using Element = typename Transport::Element;

	/// @brief Rewrites the value of the newest queued CC matching @p status and @p cc_number.
	/// @return True if a match was found and rewritten; false if the caller should append instead.
	template <typename Lane>
	bool coalesce(Lane& lane, uint8_t status, uint8_t cc_number, uint8_t value, MIDICCQueuePolicy& debt);

	/// @brief Selects and removes the next CC to emit, by debt then round-robin.
	/// @return True if a CC was selected and copied into @p out.
	template <typename Lane>
	bool pop_scheduled(Lane& lane, MIDICCQueuePolicy& debt, Element* out);

private:
	/// @brief Steps to the next queued channel CC, honouring the transport's span.
	template <typename Lane>
	static bool next_cc(Lane const& lane, uint16_t& offset, uint8_t& status, uint8_t& cc_number);
};
```

Migrate `coalesce` first — it replaces `MIDIQueueManagerUSB::coalesce_cc_message` and
`MIDIQueueManagerDIN::coalesce_cc_message`, and it is the pattern every other migration follows:

```cpp
template <typename Transport>
template <typename Lane>
bool MIDICCLanePolicy<Transport>::coalesce(Lane& lane, uint8_t status, uint8_t cc_number, uint8_t value,
                                           MIDICCQueuePolicy& debt) {
	// Find the newest queued match. Scanning unguarded is deliberate: the guard below covers only the
	// slot write, never this walk.
	int32_t latest = -1;
	uint16_t offset = 0;
	uint8_t scan_status = 0;
	uint8_t scan_cc = 0;
	while (next_cc(lane, offset, scan_status, scan_cc)) {
		if (scan_status == status && scan_cc == cc_number) {
			latest = static_cast<int32_t>(offset - Transport::cc_span);
		}
	}
	if (latest < 0) {
		return false;
	}

	{
		// A concurrent removal can advance read_pos and move the displaced head, so the offset found
		// above may now name a different message. Re-check identity under the guard rather than
		// trusting it; on a mismatch report a miss so the caller appends instead of overwriting an
		// unrelated message.
		CriticalSectionGuard guard;
		uint16_t at = static_cast<uint16_t>(latest);
		if (at + Transport::cc_span > lane.size()) {
			return false;
		}
		typename Transport::Element scratch[Transport::cc_span];
		for (uint16_t i = 0; i < Transport::cc_span; i++) {
			scratch[i] = lane.peek(static_cast<uint16_t>(at + i));
		}
		if (!Transport::is_channel_cc(scratch) || Transport::status(scratch) != status
		    || Transport::cc_number(scratch) != cc_number) {
			return false;
		}
		Transport::set_value(scratch, value);
		for (uint16_t i = 0; i < Transport::cc_span; i++) {
			lane.overwrite_at(static_cast<uint16_t>(at + i), scratch[i]);
		}
	}

	debt.bump_cc_debt(cc_number);
	return true;
}
```

Note what the traits bought: this is the USB body and the DIN body at once. `cc_span` is 1 for USB and 3 for
DIN, so the scratch copy is a single event or a three-byte message with no branching, and the guarded
re-check is written once.

Then migrate the remaining pairs in this order, deleting each transport-specific pair as its shared version lands, running the full suite after each: `pop_next_scheduled_cc_message`, `begin_cc_message_scan` + `next_cc_message` (they collapse into `next_cc`), `remove_cc_message_at`, `enqueue_message_with_cc_policy`, `reset_queue_storage`. `handle_cc_lane` and `pop_lane` stay transport-specific — they belong to the drain loops, which this task deliberately does not merge.

- [ ] **Step 5: Verify the duplication is gone**

```bash
for m in enqueue_message_with_cc_policy coalesce_cc_message pop_next_scheduled_cc_message \
         begin_cc_message_scan next_cc_message remove_cc_message_at reset_queue_storage; do
  printf "%s: " "$m"
  grep -c "MIDIQueueManagerUSB::$m\|MIDIQueueManagerDIN::$m" src/deluge/io/midi/midi_queue_manager.cpp
done
```
Expected: `0` for each. `handle_cc_lane` and `pop_lane` should still report `2` — that is intended.

- [ ] **Step 6: Run everything**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
./dbt build Debug
```
Expected: all tests pass with no changes to the existing assertions, and the firmware links. If an existing test needed changing, stop: this task is a refactor and a behaviour change means the migration is wrong.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/io/midi/ tests/unit/midi_queue_manager_tests.cpp
git commit -m "write the CC lane policy once against transport traits

Nine method pairs existed once per transport as near-identical lambda adapters over the shared policy.
USB and DIN genuinely differ in four things: element type, how identity is read out of an element, how
many elements a message spans, and how a value byte is rewritten. Those become traits, and the policy is
written once.

The drain loops stay separate. USB fills a fixed-size transfer against a message-count allowance; DIN
paces bytes against a Q8 allowance and UART space and validates variable-length heads. Those are
different algorithms, and merging them would cost more than the duplication did."
```

---

### Task 5: Drop the pass-through layer and resolve the traversal asymmetry

`MIDIQueueManagerDeviceState` wraps `MIDIQueueStorage` plus `MIDICCQueuePolicy` and forwards nearly every method verbatim — the documentation pass ended up using `@copydoc` for them because they were identical to what they wrapped. It is a layer with no behaviour.

Separately, `PriorityLaneTraversalResult::Abort` means different things per transport. For the identical condition — CC allowance exhausted, or a scheduled pop failed — USB returns `SkipLane` and falls through to the SysEx lane, while DIN returns `Abort` and halts the whole drain pass. That asymmetry looks unintentional.

**Files:**
- Modify: `src/deluge/io/midi/midi_queue_manager.h` (delete `MIDIQueueManagerDeviceState`, repoint both managers)
- Modify: `src/deluge/io/midi/midi_queue_manager.cpp` (`MIDIQueueManagerDIN::handle_cc_lane`)
- Test: `tests/unit/midi_din_drain_tests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/midi_din_drain_tests.cpp`:

```cpp
TEST(MIDIDinDrain, BlockedCCLaneDoesNotStarveSysEx) {
	// A CC lane that is merely blocked by its send allowance must not stop the pass: lower-priority
	// SysEx can still make progress. USB already falls through; DIN used to halt the whole pass.
	for (int i = 0; i < 40; i++) {
		MIDIMessage cc = MIDIMessage::cc(0, static_cast<uint8_t>(i), 64);
		cc.intent = MIDIIntent::Continuous;
		queue.enqueue_message(cc);
	}
	uint8_t const sysex[] = {0xF0, 0x7D, 0x01, 0xF7};
	CHECK_TRUE(queue.enqueue_sysex(sysex, sizeof(sysex)));

	for (int i = 0; i < 40; i++) {
		queue.consume_queued_messages(48000 + i * 48000);
	}

	auto const& sent = MidiTransportMock::sent_bytes();
	bool saw_sysex_start = false;
	for (uint8_t b : sent) {
		if (b == 0xF0) {
			saw_sysex_start = true;
		}
	}
	CHECK_TRUE(saw_sysex_start);
}
```

- [ ] **Step 2: Run it**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests -g MIDIDinDrain
```
Record the result. If it passes already, the asymmetry does not starve SysEx in practice — say so in the commit message and keep the test as a guard. If it fails, continue to Step 3.

- [ ] **Step 3: Align DIN with USB**

In `MIDIQueueManagerDIN::handle_cc_lane` in `src/deluge/io/midi/midi_queue_manager.cpp`, the final return currently reports `Abort` for both allowance exhaustion and pop failure. Split it so only undecodable head data aborts:

```cpp
	if (cc_result == MIDIQueueManager::CCScheduledPopResult::AllowanceBlocked) {
		// Blocked by the CC send allowance, not by bad data. Fall through to lower-priority lanes, as
		// USB does for the same condition, instead of halting the whole pass.
		return MIDIQueueManager::PriorityLaneTraversalResult::SkipLane;
	}
	// A scheduled pop that failed on a head this transport could not decode cannot be retried safely.
	return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
```

Then update the `Abort` documentation in `midi_queue_manager.h` to drop the note describing the divergence, since it no longer exists.

- [ ] **Step 4: Delete the pass-through layer**

Remove `MIDIQueueManagerDeviceState` from `midi_queue_manager.h`. Both managers hold storage and policy directly:

```cpp
private:
	MIDIQueueStorage<uint32_t, QUEUE_PRIORITY_COUNT, k_usb_lane_capacity> queue_storage_{};
	MIDICCQueuePolicy cc_policy_{};
	MIDICCLanePolicy<UsbTransport> cc_lane_{};
```

Every `queue_manager_.foo(...)` call in `midi_queue_manager.cpp` becomes `queue_storage_.foo(...)` or `cc_policy_.foo(...)` depending on which the pass-through was forwarding to. Find them with `grep -n "queue_manager_\." src/deluge/io/midi/midi_queue_manager.cpp`.

- [ ] **Step 5: Run everything**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
./dbt build Debug
```
Expected: all pass, firmware links.

- [ ] **Step 6: Commit**

```bash
git add src/deluge/io/midi/ tests/unit/midi_din_drain_tests.cpp
git commit -m "drop the pass-through queue layer and align DIN lane traversal with USB

MIDIQueueManagerDeviceState wrapped storage plus policy and forwarded nearly every method verbatim. Both
managers now hold the two directly.

DIN reported Abort when the CC lane was merely blocked by its send allowance, halting the whole drain
pass, where USB reports SkipLane for the identical condition and falls through to SysEx. Only
undecodable head data aborts now."
```

---

### Task 6: Split the header

`midi_queue_manager.h` holds seven classes. Tasks 3 to 5 have already moved the lane, the CC policy and the traits out; this task finishes the split and checks the result is coherent.

**Files:**
- Modify: `src/deluge/io/midi/midi_queue_manager.h`
- Verify: every file that includes it

- [ ] **Step 1: Confirm what remains**

```bash
grep -n "^class \|^template" src/deluge/io/midi/midi_queue_manager.h | grep -v ";"
wc -l src/deluge/io/midi/midi_queue_lane.h src/deluge/io/midi/midi_cc_policy.h \
      src/deluge/io/midi/midi_queue_transports.h src/deluge/io/midi/midi_queue_manager.h
```
Expected: `midi_queue_manager.h` holds only `MIDIQueueManager` (shared helpers), `MIDIQueueManagerUSB` and `MIDIQueueManagerDIN`, and is well under its previous 1136 lines.

- [ ] **Step 2: Make each new header self-sufficient**

Each of `midi_queue_lane.h`, `midi_cc_policy.h` and `midi_queue_transports.h` must compile standalone. Verify:

```bash
for h in midi_queue_lane midi_cc_policy midi_queue_transports midi_queue_manager; do
  printf "%s: " "$h"
  echo "#include \"io/midi/$h.h\"" > /tmp/hdr_$h.cpp
  echo "int main(){return 0;}" >> /tmp/hdr_$h.cpp
  g++ -std=c++23 -m32 -fsyntax-only -I src -I src/deluge /tmp/hdr_$h.cpp && echo OK || echo FAIL
done
```
Expected: `OK` for all four. Add whatever include each is missing rather than relying on include order.

- [ ] **Step 3: Build and test**

```bash
./dbt build Debug
cd tests/build && ninja UnitTests && ./unit/UnitTests
```

- [ ] **Step 4: Commit**

```bash
git add src/deluge/io/midi/
git commit -m "split the queue manager header by responsibility

midi_queue_manager.h held seven classes. The ring lane, the CC policy and the transport traits now live
in their own headers, each compiling standalone; midi_queue_manager.h keeps the shared helpers and the
two transport managers."
```

---

### Task 7: Update the design notes and assemble the PR

**Files:**
- Modify: `docs/dev/systems/midi_queue_manager.md`

- [ ] **Step 1: Bring the design notes in line**

Update these sections in `docs/dev/systems/midi_queue_manager.md`:

- **`## Main classes`** — the table still lists `MIDIQueueManagerDeviceState`. Remove that row; add rows for `MIDICCLanePolicy` ("the CC-lane policy, written once against transport traits") and `UsbTransport` / `DinTransport` ("per-transport traits: element type, identity accessors, message span, value rewrite").
- **`### Scheduled CC dequeue`** — unchanged in behaviour, but the prose says "the queue manager" where it now means the shared policy. Reword to name `MIDICCLanePolicy`.
- **`## Important invariants`** — add: "Lane capacities differ per lane and must each be a power of two." and "The queue manager never calls back into `MidiEngine`; `enqueue_message` reports that a flush is wanted and the caller decides."
- **`## Testing`** — add that the DIN drain path is now covered on the host via link-time doubles in `tests/unit/mocks/midi_transport_mock.cpp`, and remove "The transport wiring itself is not unit tested" since that is no longer true.

- [ ] **Step 2: Verify the whole thing one more time**

```bash
./dbt build Debug
cd tests/build && ninja UnitTests && ./unit/UnitTests
cd /home/kate/GitHub/DelugeFirmware-clean
toolchain/v22/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-nm --print-size --radix=d \
  build/Debug/deluge.elf | grep -iE "usbQueueManagers|connectedDINMIDIDevice"
```
Record the final memory figures; they belong in the PR description.

- [ ] **Step 3: Commit**

```bash
git add docs/dev/systems/midi_queue_manager.md
git commit -m "update MIDI queue manager design notes for the restructure"
```

- [ ] **Step 4: Remove this plan from the branch**

The branch inherits `docs/superpowers/plans/` because it was committed before branching. Process
artifacts do not belong in the PR — the spec and the previous plan were stripped for the same reason.
Keep a copy outside the tree if you still want it.

```bash
cp docs/superpowers/plans/2026-08-25-midi-queue-restructure.md /tmp/midi-restructure-plan.md
git rm -r --quiet docs/superpowers
git commit -m "remove the implementation plan from the branch

Process artifact, not something the firmware repo carries. The durable design notes live in
docs/dev/systems/midi_queue_manager.md."
```

Verify nothing references it: `git grep -n "superpowers" -- . || echo clean`

- [ ] **Step 5: Open the PR**

```bash
git push -u origin midi-queue-manager-v6
```

The PR description should carry: what the feature does, the measured memory figures before and after Task 3, the fact that the DIN drain path is now host-tested, and an explicit list of what still needs hardware validation (below).

---

## Verification

After all tasks:

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests   # all tests pass
cd - && ./dbt build Debug                               # clean link
```

## What this plan does not verify

- **Hardware behaviour.** Everything here is host tests plus a clean build. MPE expression output, MPE zone configuration via RPN, and DIN under dense CC automation all still need a device — DIN especially, where 31250 baud makes congestion far likelier than USB.
- **That the scheduling earns its complexity.** Simulation showed debt-based CC reordering is a measurable no-op until the pending CC count exceeds the per-drain allowance. Nobody has confirmed on hardware that the reordering half of the design (as opposed to the coalescing half) changes what a player hears.
- **The lane capacities in Task 3.** They are reasoned from the largest staged SysEx (1024 bytes) and from one entry per distinct CC identity, not measured against real traffic. If a lane overflows in practice it will silently drop messages, so they deserve a sanity check under load before merge.
