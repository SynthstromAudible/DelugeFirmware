# MIDI output: opt-in coalescing and note-bound ordering

Status: proposed
Date: 2026-08-25
Branch: `MIDI-Queue-Manager-v5` (on top of the MIDI Queue Manager, PR #4840)

## Problem

The MIDI Queue Manager reorders outgoing MIDI to keep clock and notes ahead of bulk CC automation. It
does this by classifying each message independently into a priority lane, then within the CC lane
coalescing by CC number and reordering by accumulated debt.

That model assumes MIDI messages are independent. They are not. MIDI uses **stateful prefixes**: a
sequence of messages where earlier ones establish context for later ones. The scheduler has no
representation of those dependencies, so it breaks them.

### Confirmed failures

**1. RPN / MPE Configuration Messages.** `MIDICable::sendRPN` (`midi_device.cpp:153`) emits five CCs:

```
CC 100 = rpnLSB    CC 101 = rpnMSB    CC 6 = valueMSB    CC 100 = 127    CC 101 = 127
```

The trailing pair is a terminator that deliberately reuses CC 100/101. Coalescing merges it into the
opening selection, and debt reordering then emits the terminator first. Driving the real
`MIDICCQueuePolicy` with `sendRPN(0, 0, 6, 4)` produces:

```
sent:     CC100=6    CC101=0    CC6=4    CC100=127  CC101=127
received: CC100=127  CC101=127  CC6=4
```

The RPN address is destroyed and the data entry lands on the null parameter. MPE zone configuration
silently never applies. Reached via `sendMCMsNowIfNeeded()` on device connect and from the
zone-member-channels menu.

**2. MPE note-on ordering.** `midi_instrument.cpp:1069-1077` sends expression before the note-on:

```cpp
outputAllMPEValuesOnMemberChannel(...)   // pitch bend, CC74, channel aftertouch -> EXPRESSION
midiEngine.sendNote(..., true, ...)      // note-on                              -> NOTES
```

`QUEUE_PRIORITY_NOTES` (1) drains before `QUEUE_PRIORITY_EXPRESSION` (2), so the note-on overtakes the
three messages queued ahead of it. Every MPE note-on starts with the previous member-channel X/Y/Z and
then snaps. The existing Hydrasynth comment at `midi_queue_manager.cpp:323` ("For MPE this leads to
ignoring note ons as the x and y resets are sent before the note on") confirms expression-before-note
is the intended order. This is the path PR #4840's own checklist marks as untested.

**3. Bank Select then Program Change.** `instrument_clip.cpp:3509-3515` sends CC 0, CC 32, then a Program
Change. All land in the CC lane. CC 0 and CC 32 can invert depending on where `next_cc_number` has
rotated to, and because scheduled pop selects from anywhere in the lane while the Program Change only
leaves from the head, a CC queued after the PC can be pulled ahead of it onto the old patch.

**4. All Notes Off.** `sendAllNotesOff` emits CC 123 into the lowest-priority channel lane. Notes queued
afterwards sit in the higher-priority NOTES lane and drain first, so "all notes off then new notes" can
invert into "new notes then all notes off", killing the notes that should have sounded.

### The general class

Coalescing assumes every CC is a continuous parameter where only the latest value matters. That is false
for any CC used as a discrete event — anything sent as 127-then-0 in quick succession has the
intermediate value swallowed and the event never fires. Findings 1 and 4 are instances of this; a mapped
momentary control would be another.

### Why a denylist does not generalise

The obvious fix is to exempt the known protocol CCs (6, 38, 96-101) and channel-mode CCs (120-127) from
coalescing and reordering. That enumerates the dependencies we happen to have found. The failure mode of
a miss is silent corruption, not a crash — nothing reports that MPE failed to configure. It also does
nothing for finding 2, which is a lane-priority problem rather than a coalescing one, and nothing for
finding 3's Program-Change interaction.

Covering all four cases by enumeration needs three separate mechanisms: a CC-number denylist, a fix for
the PC-vs-CC head-order interaction, and a cross-lane fix for MPE.

## Design

Two changes, sharing one mechanism.

### Message intent

Senders declare what a message *is*, rather than the queue inferring it from a CC number.

```cpp
/// What a queued MIDI message is, which determines whether the scheduler may merge or reorder it.
enum class MIDIIntent : uint8_t {
	/// A discrete event. Queued verbatim and kept in order relative to other Event messages on its
	/// lane. Never coalesced, never selected out of order. This is the default: a sender that says
	/// nothing gets the safe behaviour.
	Event,
	/// The current value of a continuous parameter, where a later value supersedes an earlier one.
	/// Eligible for coalescing and for debt-based reordering.
	Continuous,
	/// Expression that initialises a note and must not be overtaken by it. Queued on the notes lane
	/// so it stays ordered with the note event it accompanies.
	NoteBound,
};
```

The default is the safe one. A missed annotation costs latency, never correctness — the inverse of
today, where a missed exemption costs correctness.

`MIDIIntent` is carried on `MIDIMessage`, which is already a small POD passed by value along the whole
send path, and is never stored in a queue ring (USB packs to `uint32_t`, DIN to raw bytes). The send API
takes it as a defaulted trailing parameter:

```cpp
void sendCC(MIDISource source, int32_t channel, int32_t cc, int32_t value, int32_t filter,
            MIDIIntent intent = MIDIIntent::Event);
```

### Queue rules

`classify_message` gains one rule: `NoteBound` messages classify to `QUEUE_PRIORITY_NOTES` regardless of
status type. Everything else classifies as it does today.

`enqueue_message_with_cc_policy` coalesces only when `intent == Continuous`. `handle_cc_lane` runs
scheduled selection only when the lane head is a `Continuous` CC; an `Event` CC at the head takes the
normal FIFO `PopLane` path. `select_scheduled_cc` only ever considers `Continuous` entries as candidates.

This keeps five lanes. Adding a sixth strictly-ordered CC lane was considered and rejected: each lane is
a full-capacity ring (`std::array<MIDIQueueLane<T, Capacity>, LaneCount>`), so a sixth costs 4KB per USB
device, 24KB across the six devices.

### Ordering guarantees this produces

- Lanes are FIFO, so **messages that must stay ordered relative to each other must share a lane**. This
  is the whole of the ordered-group concept; no new queue primitive is needed.
- `Event` CCs keep strict relative order with each other. Head-swap removal moves only the head, and the
  head is only swapped when it is `Continuous`, so no `Event` message is ever displaced.
- `Continuous` CCs may be merged and reordered relative to anything.
- `NoteBound` expression stays ordered with its note-on because they share the notes lane.
- SysEx contiguity is unchanged; `sysex_drain_active_` already handles it.

### Intent per call site

Thirteen CC call sites, plus the note path:

| Call site | Intent | Why |
|---|---|---|
| `midi_param_collection.cpp` CC output | `Continuous` | Automation. The burst source this feature exists to smooth. |
| `midi_follow.cpp` knob feedback | `Continuous` | High-rate; latest value is all that matters. |
| `midi_instrument.cpp` mod wheel (CC1) | `Continuous` | Continuous expression. |
| `midi_instrument.cpp:1276` MPE Y, ongoing | `Continuous` | Per-note expression during a sounding note. |
| `outputAllMPEValuesOnMemberChannel` (`:1089`, `:1095`, `:1101`; callers `:1070`, `:1162`) | `NoteBound` | Must precede the note-on it initialises. |
| `sendRPN` (5 sends) | `Event` (default) | Stateful sequence; order and duplicates are load-bearing. |
| `sendBank`, `sendSubBank` | `Event` (default) | Prefix for a Program Change. |
| `sendAllNotesOff` | `Event` (default) | Channel-mode event; must not be merged or delayed. |

Only the `Continuous` and `NoteBound` rows change. Everything else gets correct behaviour by doing
nothing, which is the point.

## Testing

Extends `tests/unit/midi_queue_manager_tests.cpp`, which already drives the policy through its scan and
removal callbacks without needing UART or USB mocks.

- The `sendRPN` sequence survives enqueue and drain byte-for-byte, in order. This test fails on today's
  code, which is what makes it worth having.
- A `Continuous` CC coalesces; an `Event` CC with the same CC number and channel does not.
- Repeated `Event` CCs with the same number preserve every value in order (the 127-then-0 case).
- `Event` CCs keep relative order while `Continuous` ones around them are reordered.
- `NoteBound` messages classify to the notes lane and stay ahead of a note-on queued after them.
- Bank Select, Sub Bank and Program Change drain in the order sent.

The existing characterization tests must keep passing unchanged for `Continuous` traffic: this changes
which messages are eligible for the scheduler, not how the scheduler treats eligible ones.

## Risks and open questions

- **Throughput.** Making `Event` the default means any CC sender not yet annotated stops coalescing, so
  a missed `Continuous` annotation shows up as CC backlog under automation. The four sites above are
  believed to cover all high-rate sources, but that is from reading the 13 call sites, not from
  profiling on hardware.
- **`NoteBound` on the notes lane.** This puts three expression messages per note-on into the notes
  lane. At high note rates that competes with note traffic in a lane that was previously notes-only.
  Worth measuring before merge. Ongoing expression is unaffected — it stays on the expression lane.
- **Program Change is still `default:`-classified into the CC lane.** With `Event` intent it will no
  longer be overtaken by other `Event` CCs, but a `Continuous` CC can still be selected ahead of it.
  Whether bank/PC needs to also be `NoteBound`-style pinned to a higher lane is left open; finding 3 is
  the least severe of the four and the CC-lane fix addresses most of it.
- **Naming.** `MIDIIntent` rather than `CCIntent` because pitch bend and channel aftertouch are equally
  continuous and may want the same treatment later. Nothing in this spec depends on that extension.
- **Scope.** This changes the coalescing contract, which is the design premise of PR #4840. It should be
  Sean's call as the PR author; the evidence above is the input to that decision.

## Not in scope

- Coalescing pitch bend or channel aftertouch. They are continuous and could benefit, but the expression
  lane is FIFO today and nothing here depends on changing that.
- The cross-lane hazard in general (a low-priority message that must precede a high-priority one).
  `NoteBound` solves the one confirmed instance; a general mechanism is not justified by one case.
- Anything on the MIDI input path. These are output-queue changes only.

## Follow-up

On implementation, the durable parts of this document should be folded into
`src/deluge/io/midi/midi_queue_manager.md`, which is where this feature's design notes live.
