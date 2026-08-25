# MIDI Intent Opt-In Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make MIDI output coalescing and reordering opt-in, so discrete event sequences (RPN, bank select, all-notes-off, MPE note initialisation) keep their order and their duplicate values.

**Architecture:** A `MIDIIntent` enum rides on `MIDIMessage` and is consumed by `classify_message`, which routes messages to priority lanes. Intent affects *classification only* — nothing in the dequeue path changes, because routing `Event` CCs away from the CC lane leaves that lane holding only `Continuous` entries, where coalescing and debt reordering are unconditionally correct. Lanes are FIFO, so "these messages must stay ordered" is expressed as "these messages share a lane."

**Tech Stack:** C++23, ARM GCC 14.2 (`./dbt build Debug`), CppUTest host unit tests (`tests/unit`, built with `cmake -S tests -B tests/build && ninja -C tests/build UnitTests`).

**Spec:** `docs/superpowers/specs/2026-08-25-midi-intent-opt-in-design.md`

## Global Constraints

- Branch: `MIDI-Queue-Manager-v5`, on top of commits `4f67b42b` and `2f4749ef`.
- Firmware verification is `./dbt build Debug` only. Do not build Release.
- Method naming in new code follows the surrounding file. `midi_queue_manager.*` uses `snake_case`; `midi_engine.*` and `midi_device.*` use `camelCase`. Match the file you are editing, not a global rule.
- Copyright headers: do not add or modify them. Every file in this plan already exists.
- `MIDIMessage` must stay an aggregate — no user-declared constructors. Adding a member with a default member initialiser is fine in C++23.
- Do not change `handle_cc_lane`, `select_scheduled_cc`, `enqueue_message_with_cc_policy`, or any dequeue logic. If a task seems to require it, stop: the design intends intent to be invisible to the consumer.
- Run the full unit suite (`./unit/UnitTests` from `tests/build`), not just the MIDI groups — 165 tests currently pass.

---

### Task 1: Intent-aware, unit-testable classification

`classify_message` currently lives in `midi_queue_manager.cpp`, which pulls in `midi_engine.h` and the UART headers, so it cannot be reached from the host test harness. It is pure logic — it touches only `MIDIMessage`, `MIDIStatusType`, and two constants from `definitions_cxx.hpp`, all of which `midi_queue_manager.h` already includes. Moving it to the header is what makes this whole change testable.

**Files:**
- Modify: `src/deluge/model/midi/message.h` (add `MIDIIntent`, add `MIDIMessage::intent`)
- Modify: `src/deluge/io/midi/midi_queue_manager.h:104` (replace the declaration with an inline definition)
- Modify: `src/deluge/io/midi/midi_queue_manager.cpp:146-176` (delete the out-of-line definition)
- Test: `tests/unit/midi_queue_manager_tests.cpp`

**Interfaces:**
- Produces: `enum class MIDIIntent : uint8_t { Event, Continuous, NoteBound }` in `message.h`; `MIDIMessage::intent` defaulting to `MIDIIntent::Event`; `static QueuePriority MIDIQueueManager::classify_message(MIDIMessage)` now defined inline in the header.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/midi_queue_manager_tests.cpp`:

```cpp
// --- Message intent and lane classification ---
//
// Intent decides which lane a message lands in, and lanes are FIFO, so "must stay ordered" is
// expressed as "must share a lane". Only Continuous messages reach the reorderable CC lane.

TEST_GROUP(MIDIMessageClassification){};

TEST(MIDIMessageClassification, DefaultIntentIsEvent) {
	// A sender that says nothing must get the conservative behaviour.
	MIDIMessage m = MIDIMessage::cc(0, 74, 100);
	CHECK(m.intent == MIDIIntent::Event);
}

TEST(MIDIMessageClassification, ContinuousCCGoesToTheScheduledLane) {
	MIDIMessage m = MIDIMessage::cc(0, 20, 64);
	m.intent = MIDIIntent::Continuous;
	CHECK(MIDIQueueManager::classify_message(m) == QUEUE_PRIORITY_CC);
}

TEST(MIDIMessageClassification, EventCCAvoidsTheScheduledLane) {
	// Bank select, RPN and friends must never be coalesced or reordered.
	MIDIMessage m = MIDIMessage::cc(0, 100, 6);
	CHECK(MIDIQueueManager::classify_message(m) == QUEUE_PRIORITY_EXPRESSION);
}

TEST(MIDIMessageClassification, NoteBoundSharesTheNotesLane) {
	// Expression that initialises a note, and All Notes Off, must not be overtaken by note traffic.
	MIDIMessage pitch = MIDIMessage::pitchBend(0, 8192);
	pitch.intent = MIDIIntent::NoteBound;
	CHECK(MIDIQueueManager::classify_message(pitch) == QUEUE_PRIORITY_NOTES);

	MIDIMessage allNotesOff = MIDIMessage::cc(0, 123, 0);
	allNotesOff.intent = MIDIIntent::NoteBound;
	CHECK(MIDIQueueManager::classify_message(allNotesOff) == QUEUE_PRIORITY_NOTES);
}

TEST(MIDIMessageClassification, ProgramChangeIsOrdered) {
	// Program change follows bank select, so it must not sit in the reorderable lane.
	MIDIMessage m = MIDIMessage::programChange(0, 5);
	CHECK(MIDIQueueManager::classify_message(m) == QUEUE_PRIORITY_EXPRESSION);
}

TEST(MIDIMessageClassification, UnchangedClassificationsStillHold) {
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::noteOn(0, 60, 100)) == QUEUE_PRIORITY_NOTES);
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::noteOff(0, 60, 0)) == QUEUE_PRIORITY_NOTES);
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::pitchBend(0, 8192)) == QUEUE_PRIORITY_EXPRESSION);
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::channelAftertouch(0, 64)) == QUEUE_PRIORITY_EXPRESSION);

	// Mod wheel and MPE Y stay on the expression lane whatever their intent.
	MIDIMessage modWheel = MIDIMessage::cc(0, CC_EXTERNAL_MOD_WHEEL, 64);
	modWheel.intent = MIDIIntent::Continuous;
	CHECK(MIDIQueueManager::classify_message(modWheel) == QUEUE_PRIORITY_EXPRESSION);

	MIDIMessage mpeY = MIDIMessage::cc(0, CC_EXTERNAL_MPE_Y, 64);
	mpeY.intent = MIDIIntent::Continuous;
	CHECK(MIDIQueueManager::classify_message(mpeY) == QUEUE_PRIORITY_EXPRESSION);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd tests/build && ninja UnitTests
```
Expected: compile FAILS with `'MIDIIntent' was not declared` and `'intent' is not a member of 'MIDIMessage'`.

- [ ] **Step 3: Add the intent enum and field**

In `src/deluge/model/midi/message.h`, immediately above `struct MIDIMessage`:

```cpp
/// What a queued MIDI message is, which decides whether the output scheduler may merge or reorder it.
///
/// The default is the conservative one: a sender that says nothing gets verbatim, in-order delivery.
/// Only a sender that knows its messages are redundant opts into merging. That way a missed annotation
/// costs latency, never correctness.
enum class MIDIIntent : uint8_t {
	/// A discrete event. Queued verbatim and kept in order relative to other events; never coalesced,
	/// never reordered. RPN sequences, bank selects and program changes depend on this.
	Event,
	/// The current value of a continuous parameter, where a later value supersedes an earlier one.
	/// Eligible for coalescing and debt-based reordering. Automation and knob feedback use this.
	Continuous,
	/// A message that must stay ordered with the note stream: expression that initialises a note and
	/// must not be overtaken by it, or All Notes Off, which the notes queued after it must not overtake.
	NoteBound,
};
```

Then add as the last member of `struct MIDIMessage`, after `data2`:

```cpp
	/// How the output scheduler may treat this message. Defaults to the conservative Event, so the
	/// existing designated-initialiser constructors below need no changes.
	MIDIIntent intent = MIDIIntent::Event;
```

- [ ] **Step 4: Move classification into the header and honour intent**

In `src/deluge/io/midi/midi_queue_manager.h`, replace line 104:

```cpp
	/// Classifies an outgoing MIDI message into priority groups.
	static QueuePriority classify_message(MIDIMessage message);
```

with:

```cpp
	/// Classifies an outgoing MIDI message into priority groups.
	///
	/// Defined inline rather than in the .cpp so it can be unit tested without the UART and USB layers.
	/// Intent is consumed here and nowhere else: routing Event CCs away from the CC lane leaves that
	/// lane holding only Continuous entries, which is what lets the dequeue path coalesce and reorder
	/// unconditionally without needing to know any entry's intent (there is nowhere to store it - a USB
	/// entry is a fully packed uint32 and a DIN entry is a raw byte).
	static QueuePriority classify_message(MIDIMessage message) {
		if (message.isSystemMessage()) {
			// Keep system/realtime messages in the highest-priority lane.
			return QUEUE_PRIORITY_CLOCK;
		}

		if (message.intent == MIDIIntent::NoteBound) {
			// Must stay ordered with the note stream, and lanes are FIFO, so it has to share the notes
			// lane. Covers MPE expression that initialises a note, and All Notes Off.
			return QUEUE_PRIORITY_NOTES;
		}

		switch (static_cast<MIDIStatusType>(message.statusType)) {
		case MIDIStatusType::NoteOff:
		case MIDIStatusType::NoteOn:
			// Note on/off events are timing-sensitive, but below clock/system messages.
			return QUEUE_PRIORITY_NOTES;

		case MIDIStatusType::PolyphonicAftertouch:
		case MIDIStatusType::ChannelAftertouch:
		case MIDIStatusType::PitchBend:
			// Expression data is important for feel, but can sit behind notes.
			return QUEUE_PRIORITY_EXPRESSION;

		case MIDIStatusType::ControlChange:
			if (message.data1 == CC_EXTERNAL_MOD_WHEEL || message.data1 == CC_EXTERNAL_MPE_Y) {
				// Mod wheel and MPE Y-axis are expressive CCs that should be prioritized above other CCs.
				return QUEUE_PRIORITY_EXPRESSION;
			}
			if (message.intent == MIDIIntent::Continuous) {
				// Only continuous parameter updates may be merged and reordered, so only they belong in
				// the scheduled CC lane.
				return QUEUE_PRIORITY_CC;
			}
			// Discrete CC events keep their order and their duplicate values. The expression lane is
			// strictly FIFO and never coalesced, which is exactly the behaviour they need.
			return QUEUE_PRIORITY_EXPRESSION;

		default:
			// Program change and unknown channel messages are discrete events that follow a prefix.
			return QUEUE_PRIORITY_EXPRESSION;
		}
	}
```

- [ ] **Step 5: Delete the out-of-line definition**

Remove the whole `QueuePriority MIDIQueueManager::classify_message(MIDIMessage message) { ... }` body from `src/deluge/io/midi/midi_queue_manager.cpp` (currently lines 146-176, ending with the closing brace after the `default:` return). Leave the surrounding functions untouched.

- [ ] **Step 6: Run tests to verify they pass**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: PASS, 171 tests (165 existing + 6 new), 0 failures.

- [ ] **Step 7: Verify the firmware still builds**

```bash
./dbt build Debug
```
Expected: links with no errors. The `LOAD segment with RWX permissions` warning is pre-existing.

- [ ] **Step 8: Commit**

```bash
git add src/deluge/model/midi/message.h src/deluge/io/midi/midi_queue_manager.h \
        src/deluge/io/midi/midi_queue_manager.cpp tests/unit/midi_queue_manager_tests.cpp
git commit -m "classify MIDI output by sender intent

Adds MIDIIntent, carried on MIDIMessage and consumed only by classify_message. Continuous messages keep
going to the scheduled CC lane where they may be coalesced and reordered; discrete events now route to
the strictly-FIFO expression lane instead, so their order and their duplicate values survive. Program
changes move with them, since they follow a bank-select prefix.

classify_message moves into the header so it can be unit tested without the UART and USB layers. It is
pure logic and gains no dependencies by moving.

No sender opts in yet, so this commit changes behaviour only for program change and for CCs that were
already reaching the CC lane - both of which move to strictly ordered delivery."
```

---

### Task 2: Carry intent through the USB enqueue path

DIN already carries intent for free: `sendSerialMidi` passes the whole `MIDIMessage` to `ConnectedDINMIDIDevice::enqueue_message(MIDIMessage)`. USB packs the message into a `uint32_t` via `setupUSBMessage()` before queueing, which discards it, so USB needs an explicit parameter.

**Files:**
- Modify: `src/deluge/io/midi/midi_queue_manager.h:655` (`MIDIQueueManagerUSB::enqueue_message`)
- Modify: `src/deluge/io/midi/midi_queue_manager.cpp` (`enqueue_message`, `classify_packed_usb_priority`)
- Modify: `src/deluge/io/midi/midi_device_manager.h:60`, `src/deluge/io/midi/midi_device_manager.cpp` (`ConnectedUSBMIDIDevice::enqueue_message`)
- Modify: `src/deluge/io/midi/midi_engine.cpp` (`sendUsbMidi`, ~line 582)
- Modify: `src/deluge/io/midi/cable_types/usb_common.cpp:53,112,137`

**Interfaces:**
- Consumes: `MIDIIntent` from Task 1.
- Produces: `void ConnectedUSBMIDIDevice::enqueue_message(uint32_t fullMessage, MIDIIntent intent)`; `void MIDIQueueManagerUSB::enqueue_message(uint32_t full_message, MIDIIntent intent)`; `static QueuePriority MIDIQueueManagerUSB::classify_packed_usb_priority(uint32_t packed, MIDIIntent intent)`.

There is no host-testable seam here — this is plumbing through files that need the USB and UART layers. Its verification is that Task 1's classification tests still pass and the firmware builds. Do not add a mock to create a seam; the behaviour is already covered by Task 1.

- [ ] **Step 1: Widen the queue manager signatures**

In `src/deluge/io/midi/midi_queue_manager.h`, change the public declaration:

```cpp
	void enqueue_message(uint32_t full_message, MIDIIntent intent);
```

and the private one:

```cpp
	static QueuePriority classify_packed_usb_priority(uint32_t packed, MIDIIntent intent);
```

In `src/deluge/io/midi/midi_queue_manager.cpp`, change both definitions to match, and thread intent into the synthetic message:

```cpp
QueuePriority MIDIQueueManagerUSB::classify_packed_usb_priority(uint32_t packed, MIDIIntent intent) {
	if (is_usb_sysex_event(packed)) {
		// SysEx USB-MIDI events use CIN 0x4..0x7 and are already chunked by the caller.
		return QUEUE_PRIORITY_SYSEX;
	}

	// Non-SysEx events are classified by decoding the MIDI status/data bytes.
	uint8_t status = status_byte(packed);
	MIDIMessage decoded{
	    .statusType = static_cast<uint8_t>((status >> 4) & 0x0F),
	    .channel = static_cast<uint8_t>(status & 0x0F),
	    .data1 = data_1(packed),
	    .data2 = data_2(packed),
	    .intent = intent,
	};
	return MIDIQueueManager::classify_message(decoded);
}
```

In `MIDIQueueManagerUSB::enqueue_message`, change the signature to `(uint32_t full_message, MIDIIntent intent)` and the classification call to `classify_packed_usb_priority(full_message, intent)`. Leave everything else in that function alone.

- [ ] **Step 2: Widen the device wrapper**

In `src/deluge/io/midi/midi_device_manager.h`:

```cpp
	/// Classifies, optionally coalesces, and enqueues one outgoing MIDI message into USB priority lanes.
	void enqueue_message(uint32_t fullMessage, MIDIIntent intent);
```

In `src/deluge/io/midi/midi_device_manager.cpp`:

```cpp
void ConnectedUSBMIDIDevice::enqueue_message(uint32_t fullMessage, MIDIIntent intent) {
	queue_manager().enqueue_message(fullMessage, intent);
}
```

- [ ] **Step 3: Pass intent at the three call sites**

In `src/deluge/io/midi/midi_engine.cpp`, inside `sendUsbMidi` (the call currently reading `connectedDevice->enqueue_message(usb_cable_message);`):

```cpp
						connectedDevice->enqueue_message(usb_cable_message, message.intent);
```

In `src/deluge/io/midi/cable_types/usb_common.cpp` line 53, which has the `MIDIMessage message` parameter in scope:

```cpp
				connectedDevice->enqueue_message(usb_cable_message, message.intent);
```

At lines 112 and 137, both on the SysEx path, there is no `MIDIMessage`. SysEx is classified by its CIN before intent is consulted, so the value is irrelevant; pass the default explicitly rather than adding a parameter to `sendSysex`:

```cpp
		connectedDevice->enqueue_message(packed, MIDIIntent::Event);
```

- [ ] **Step 4: Build the firmware**

```bash
./dbt build Debug
```
Expected: links with no errors. If the compiler reports a missing argument at a call site not listed above, add `MIDIIntent::Event` there and note it in the commit message — the list was derived from `grep -n "enqueue_message" src/deluge/io/midi/cable_types/usb_common.cpp src/deluge/io/midi/midi_engine.cpp`.

- [ ] **Step 5: Re-run the unit tests**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: PASS, 171 tests, 0 failures. Nothing should change — this task moves intent, it does not act on it.

- [ ] **Step 6: Commit**

```bash
git add src/deluge/io/midi/midi_queue_manager.h src/deluge/io/midi/midi_queue_manager.cpp \
        src/deluge/io/midi/midi_device_manager.h src/deluge/io/midi/midi_device_manager.cpp \
        src/deluge/io/midi/midi_engine.cpp src/deluge/io/midi/cable_types/usb_common.cpp
git commit -m "carry message intent through the USB enqueue path

DIN passes the whole MIDIMessage to its queue so intent rides along already. USB packs to a uint32 before
queueing, which drops it, so the packed enqueue takes it as an explicit parameter and rebuilds the
synthetic message with it for classification.

Plumbing only; no behaviour change until senders opt in."
```

---

### Task 3: Annotate the senders

Four sites opt into `Continuous`; two opt into `NoteBound`. Everything else is correct by default, which is the point of the design — `sendRPN`, `sendBank` and `sendSubBank` are not touched.

**Files:**
- Modify: `src/deluge/io/midi/midi_engine.h:69,96,98` and `src/deluge/io/midi/midi_engine.cpp` (`sendCC`, `sendPitchBend`, `sendChannelAftertouch`, `sendAllNotesOff`)
- Modify: `src/deluge/modulation/midi/midi_param_collection.cpp` (automation CC output)
- Modify: `src/deluge/io/midi/midi_follow.cpp` (knob feedback)
- Modify: `src/deluge/model/instrument/midi_instrument.cpp:1089,1095,1101,1276`

**Interfaces:**
- Consumes: `MIDIIntent` from Task 1.
- Produces: `sendCC`, `sendPitchBend` and `sendChannelAftertouch` each gain a trailing `MIDIIntent intent = MIDIIntent::Event` parameter.

- [ ] **Step 1: Widen the send API**

In `src/deluge/io/midi/midi_engine.h`, add the trailing defaulted parameter to the three declarations:

```cpp
	void sendCC(MIDISource source, int32_t channel, int32_t cc, int32_t value, int32_t filter,
	            MIDIIntent intent = MIDIIntent::Event);
	void sendPitchBend(MIDISource source, int32_t channel, uint16_t bend, int32_t filter,
	                   MIDIIntent intent = MIDIIntent::Event);
	void sendChannelAftertouch(MIDISource source, int32_t channel, int32_t value, int32_t filter,
	                           MIDIIntent intent = MIDIIntent::Event);
```

In `src/deluge/io/midi/midi_engine.cpp`, replace all three definitions. Note that `sendCC` and `sendChannelAftertouch` clamp through `toDataByte()` and `sendPitchBend` does not; keep that exactly as it is.

```cpp
void MidiEngine::sendCC(MIDISource source, int32_t channel, int32_t cc, int32_t value, int32_t filter,
                        MIDIIntent intent) {
	MIDIMessage message = MIDIMessage::cc(channel, cc, toDataByte(value));
	message.intent = intent;
	sendMidi(source, message, filter);
}
```

```cpp
void MidiEngine::sendPitchBend(MIDISource source, int32_t channel, uint16_t bend, int32_t filter,
                               MIDIIntent intent) {
	MIDIMessage message = MIDIMessage::pitchBend(channel, bend);
	message.intent = intent;
	sendMidi(source, message, filter);
}
```

```cpp
void MidiEngine::sendChannelAftertouch(MIDISource source, int32_t channel, int32_t value, int32_t filter,
                                       MIDIIntent intent) {
	MIDIMessage message = MIDIMessage::channelAftertouch(channel, toDataByte(value));
	message.intent = intent;
	sendMidi(source, message, filter);
}
```

- [ ] **Step 2: Mark All Notes Off as note-bound**

In `src/deluge/io/midi/midi_engine.cpp`:

```cpp
void MidiEngine::sendAllNotesOff(MIDISource source, int32_t channel, int32_t filter) {
	// Must not be overtaken by the notes queued after it, or it silences them instead of the notes it
	// was meant to stop. NoteBound puts it on the notes lane, which is FIFO.
	MIDIMessage message = MIDIMessage::cc(channel, 123, 0);
	message.intent = MIDIIntent::NoteBound;
	sendMidi(source, message, filter);
}
```

- [ ] **Step 3: Opt the continuous senders in**

In `src/deluge/modulation/midi/midi_param_collection.cpp`, at the `sendCC(source, masterChannel, cc, newValueSmall + 64, ...)` call, add the trailing argument:

```cpp
	                     MIDIIntent::Continuous);
```

In `src/deluge/io/midi/midi_follow.cpp`, at the `sendCC(this, channel, ccNumber, knobPos + kKnobPosOffset, midiOutputFilter)` call, likewise append `MIDIIntent::Continuous`.

In `src/deluge/model/instrument/midi_instrument.cpp:1276` (ongoing per-note MPE Y during a sounding note), append `MIDIIntent::Continuous`.

If a file does not already see `MIDIIntent`, add `#include "model/midi/message.h"`. `midi_engine.h` already includes it, so any file calling `midiEngine.sendCC()` reaches it transitively; add the include only where the compiler reports `MIDIIntent` undeclared.

- [ ] **Step 4: Mark the MPE note-initialisation expression**

In `src/deluge/model/instrument/midi_instrument.cpp`, `outputAllMPEValuesOnMemberChannel` sends three messages that must precede the note-on queued after them. Add `MIDIIntent::NoteBound` as the trailing argument to all three, and record why:

```cpp
void MIDIInstrument::outputAllMPEValuesOnMemberChannel(int16_t const* mpeValuesToUse, int32_t outputMemberChannel) {
	int32_t channel = getChannel();
	// These initialise the note that the caller sends immediately afterwards. The notes lane has higher
	// priority than the expression lane, so without NoteBound the note-on overtakes them and the note
	// starts on the previous member-channel values before snapping. NoteBound puts them on the notes
	// lane, which is FIFO, so the order the caller wrote is the order that goes out.
	{ // X
		int32_t outputValue14 = mpeValuesToUse[0] >> 2;
		mpeOutputMemberChannels[outputMemberChannel].lastXValueSent = outputValue14;
		int32_t outputValue14Unsigned = outputValue14 + 8192;
		midiEngine.sendPitchBend(this, outputMemberChannel, outputValue14Unsigned, channel,
		                         MIDIIntent::NoteBound);
	}

	{ // Y
		int32_t outputValue7 = mpeValuesToUse[1] >> 9;
		mpeOutputMemberChannels[outputMemberChannel].lastYAndZValuesSent[0] = outputValue7;
		midiEngine.sendCC(this, outputMemberChannel, outputMPEY, outputValue7 + 64, channel,
		                  MIDIIntent::NoteBound);
	}

	{ // Z
		int32_t outputValue7 = mpeValuesToUse[2] >> 8;
		mpeOutputMemberChannels[outputMemberChannel].lastYAndZValuesSent[1] = outputValue7;
		midiEngine.sendChannelAftertouch(this, outputMemberChannel, outputValue7, channel,
		                                 MIDIIntent::NoteBound);
	}
}
```

- [ ] **Step 5: Build and test**

```bash
./dbt build Debug
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: firmware links; 171 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/deluge/io/midi/midi_engine.h src/deluge/io/midi/midi_engine.cpp \
        src/deluge/modulation/midi/midi_param_collection.cpp src/deluge/io/midi/midi_follow.cpp \
        src/deluge/model/instrument/midi_instrument.cpp
git commit -m "opt the continuous MIDI senders into coalescing

Automation output, MIDI-follow knob feedback and ongoing per-note MPE Y declare themselves Continuous, so
they keep the coalescing and reordering the queue manager was built for. Everything else keeps the
conservative default.

MPE note-initialisation expression and All Notes Off declare NoteBound, which puts them on the notes lane
so the notes they bracket cannot overtake them. sendRPN, sendBank and sendSubBank are deliberately
untouched: Event is already correct for them."
```

---

### Task 4: Regression tests for the four ordering defects, and fold the design notes in

Each of the four defects from the spec becomes a test asserting the messages land on lanes that preserve their order. These are classification-level tests, which is where the whole fix lives — the dequeue path is unchanged and already covered.

**Files:**
- Test: `tests/unit/midi_queue_manager_tests.cpp`
- Modify: `src/deluge/io/midi/midi_queue_manager.md`

- [ ] **Step 1: Write the failing regression tests**

Append to `tests/unit/midi_queue_manager_tests.cpp`:

```cpp
// --- Regression tests for the ordering defects in docs/superpowers/specs/2026-08-25-midi-intent-opt-in-design.md
//
// Each of these sequences was scrambled by coalescing or reordering. They are asserted at the
// classification level because that is where the fix lives: a lane is FIFO, so co-locating an ordered
// sequence on one lane is what preserves it.

namespace {
/// Builds the CC an ordered protocol sequence sends: default Event intent.
MIDIMessage eventCC(uint8_t channel, uint8_t cc, uint8_t value) {
	return MIDIMessage::cc(channel, cc, value);
}
} // namespace

TEST_GROUP(MIDIOrderingRegressions){};

TEST(MIDIOrderingRegressions, RPNSequenceStaysOnOneOrderedLane) {
	// sendRPN() emits CC100, CC101, CC6, then CC100=127 and CC101=127 as a terminator. Coalescing used
	// to merge the terminator into the opening selection and reordering emitted it first, so the MPE
	// configuration address was destroyed and the data entry landed on the null parameter.
	MIDIMessage sequence[] = {
	    eventCC(0, 100, 6), eventCC(0, 101, 0), eventCC(0, 6, 4), eventCC(0, 100, 127), eventCC(0, 101, 127),
	};
	for (MIDIMessage m : sequence) {
		QueuePriority lane = MIDIQueueManager::classify_message(m);
		CHECK(lane != QUEUE_PRIORITY_CC); // never the coalescing/reordering lane
		CHECK(lane == QUEUE_PRIORITY_EXPRESSION);
	}
}

TEST(MIDIOrderingRegressions, MPENoteInitialisationSharesTheNoteLane) {
	// outputAllMPEValuesOnMemberChannel() sends these immediately before a note-on. The notes lane
	// outranks the expression lane, so without NoteBound the note-on overtook them.
	MIDIMessage x = MIDIMessage::pitchBend(1, 8192);
	MIDIMessage y = MIDIMessage::cc(1, CC_EXTERNAL_MPE_Y, 64);
	MIDIMessage z = MIDIMessage::channelAftertouch(1, 0);
	x.intent = MIDIIntent::NoteBound;
	y.intent = MIDIIntent::NoteBound;
	z.intent = MIDIIntent::NoteBound;

	QueuePriority noteLane = MIDIQueueManager::classify_message(MIDIMessage::noteOn(1, 60, 100));
	CHECK(MIDIQueueManager::classify_message(x) == noteLane);
	CHECK(MIDIQueueManager::classify_message(y) == noteLane);
	CHECK(MIDIQueueManager::classify_message(z) == noteLane);
}

TEST(MIDIOrderingRegressions, BankSelectAndProgramChangeShareAnOrderedLane) {
	// instrument_clip.cpp sends bank MSB, bank LSB, then the program change. A reordered CC could
	// previously be pulled ahead of the program change and land on the old patch.
	QueuePriority bankMSB = MIDIQueueManager::classify_message(eventCC(0, 0, 3));
	QueuePriority bankLSB = MIDIQueueManager::classify_message(eventCC(0, 32, 1));
	QueuePriority pgm = MIDIQueueManager::classify_message(MIDIMessage::programChange(0, 5));

	CHECK(bankMSB == bankLSB);
	CHECK(bankLSB == pgm);
	CHECK(pgm != QUEUE_PRIORITY_CC);
}

TEST(MIDIOrderingRegressions, AllNotesOffSharesTheNoteLane) {
	// Queued on a lower-priority lane, notes sent after it drained first and were then silenced by it.
	MIDIMessage allNotesOff = MIDIMessage::cc(0, 123, 0);
	allNotesOff.intent = MIDIIntent::NoteBound;
	CHECK(MIDIQueueManager::classify_message(allNotesOff)
	      == MIDIQueueManager::classify_message(MIDIMessage::noteOn(0, 60, 100)));
}

TEST(MIDIOrderingRegressions, MomentaryCCKeepsBothOfItsValues) {
	// A CC used as a trigger sends 127 then 0. Coalescing would merge them and the trigger would never
	// fire. Event intent keeps it off the coalescing lane entirely.
	CHECK(MIDIQueueManager::classify_message(eventCC(0, 64, 127)) != QUEUE_PRIORITY_CC);
	CHECK(MIDIQueueManager::classify_message(eventCC(0, 64, 0)) != QUEUE_PRIORITY_CC);
}
```

- [ ] **Step 2: Run them**

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests
```
Expected: PASS, 176 tests, 0 failures. These pass immediately given Tasks 1-3; they exist to lock the behaviour, and each one fails if its annotation is later removed.

- [ ] **Step 3: Record the contract in the feature's design doc**

Append to `src/deluge/io/midi/midi_queue_manager.md`:

```markdown
## Message intent

Coalescing and reordering are opt-in. A sender declares what a message is via `MIDIIntent` on
`MIDIMessage`, and `classify_message()` routes on it:

- `Event` (the default) - a discrete event. Routed to the expression lane, which is strictly FIFO and
  never coalesced, so its order and its duplicate values survive. RPN sequences, bank selects, program
  changes and momentary CCs rely on this.
- `Continuous` - the current value of a parameter, where a later value supersedes an earlier one. Routed
  to the CC lane, where it may be coalesced and reordered by CC debt.
- `NoteBound` - must stay ordered with the note stream. Routed to the notes lane. Used by the MPE
  expression that initialises a note, and by All Notes Off.

Intent is consumed only by classification. It is not stored per queue entry and cannot be - a USB entry
is a fully packed `uint32_t` and a DIN entry is a raw byte - so the dequeue path never sees it. Keeping
`Event` traffic out of the CC lane is what makes the coalescing and debt reordering there
unconditionally correct.

Because lanes are FIFO, the rule for any ordered sequence is: **messages that must stay ordered relative
to each other must share a lane.** That is why `NoteBound` exists rather than a separate grouping
mechanism.

The default is deliberately the conservative one, so that a sender which is never annotated loses
coalescing (a latency cost) rather than ordering (a correctness cost).
```

- [ ] **Step 4: Commit**

```bash
git add tests/unit/midi_queue_manager_tests.cpp src/deluge/io/midi/midi_queue_manager.md
git commit -m "pin the four MIDI ordering defects with regression tests

One test per defect from the design doc: the RPN sequence, MPE note initialisation, bank select before
program change, and All Notes Off. Each asserts the sequence lands on a lane that preserves its order,
which is where the fix lives - the dequeue path is unchanged.

Also folds the intent contract into the queue manager's design notes."
```

---

## Verification

After all four tasks:

```bash
cd tests/build && ninja UnitTests && ./unit/UnitTests   # expect 176 tests, 0 failures
cd - && ./dbt build Debug                               # expect a clean link
```

## What this plan does not verify

- **Hardware behaviour.** Every test here is host-side classification. That the MPE glitch is actually gone, and that MPE zone configuration now applies, needs a device and an MPE-capable synth. Same for DIN, where the 31250-baud limit makes CC congestion far more likely than on USB.
- **Throughput after the default flips.** Any CC sender not annotated `Continuous` stops coalescing. The four annotated sites were derived by reading all 13 `sendCC` call sites, not by profiling. If CC backlog appears under heavy automation, look for an unannotated sender before changing the design.
- **Expression lane contention.** Event CCs and program changes now share that lane with pitch bend and channel aftertouch, and `NoteBound` puts three messages per note-on onto the notes lane. Both are expected to be low-rate, but neither was measured.
