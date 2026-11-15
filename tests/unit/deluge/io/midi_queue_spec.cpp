#include "io/midi/midi_queue.h"
#include "libdeluge/system.h"

#include <cstdint>

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace {

QueuedMIDI noteOn(uint8_t note, OutputTime due) {
	return QueuedMIDI{.message = MIDIMessage::noteOn(0, note, 100), .filter = 0, .usb = true, .din = true, .due = due};
}

} // namespace

// clang-format off
describe midi_queue_group("MIDI queue", ${
	it("starts empty", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		OutputTime due = 0;
		expect(queue.empty()).to_be_true();
		expect(queue.nextDue(due, guard)).to_be_false();
		expect(queue.frontDue(0, true, guard)).to_be_null();
	});

	it("keeps payload and due time", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		expect(queue.push(noteOn(60, 1500), 1000, guard)).to_be_true();
		OutputTime due = 0;
		expect(queue.nextDue(due, guard)).to_be_true();
		expect(due).to_equal(1500u);
		const QueuedMIDI* front = queue.frontDue(1500, false, guard);
		expect(front).not_().to_be_null();
		if (front == nullptr) {
			return;
		}
		expect(front->message.data1).to_equal(60);
		expect(front->usb).to_be_true();
		expect(front->din).to_be_true();
	});

	it("only due messages come out", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		queue.push(noteOn(60, 1100), 1000, guard);
		queue.push(noteOn(61, 1200), 1000, guard);
		expect(queue.frontDue(1099, false, guard)).to_be_null();
		expect(queue.frontDue(1100, false, guard)).not_().to_be_null();
		queue.pop(guard);
		expect(queue.frontDue(1150, false, guard)).to_be_null();
		expect(queue.frontDue(1150, true, guard)).not_().to_be_null();
	});

	it("earlier due behind later waits to keep order", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		queue.push(noteOn(60, 1500), 1000, guard);
		queue.push(noteOn(61, 1100), 1000, guard);
		// The second is due, but stays behind the first so nothing goes out of order
		expect(queue.frontDue(1200, false, guard)).to_be_null();
		const QueuedMIDI* front = queue.frontDue(1500, false, guard);
		expect(front).not_().to_be_null();
		if (front == nullptr) {
			return;
		}
		expect(front->message.data1).to_equal(60);
	});

	it("stale due times are due now", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		// In the past
		queue.push(noteOn(60, 900), 1000, guard);
		// Further ahead than any audio buffer, e.g. left over from before the render stalled
		queue.push(noteOn(61, 1000 + MIDIQueue::kMaxMicrosAhead + 1), 1000, guard);
		OutputTime due = 0;
		queue.nextDue(due, guard);
		expect(due).to_equal(1000u);
		queue.pop(guard);
		queue.nextDue(due, guard);
		expect(due).to_equal(1000u);
	});

	it("due comparison handles clock wrap", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		OutputTime now = UINT32_MAX - 100;
		queue.push(noteOn(60, now + 200), now, guard); // wraps past zero
		OutputTime due = 0;
		queue.nextDue(due, guard);
		expect(due).to_equal(99u);
		expect(queue.frontDue(now + 199, false, guard)).to_be_null();
		expect(queue.frontDue(now + 200, false, guard)).not_().to_be_null();
	});

	it("full queue rejects until space freed", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		for (size_t i = 0; i < MIDIQueue::kCapacity; i++) {
			expect(queue.push(noteOn(60, 1000), 1000, guard)).to_be_true();
		}
		expect(queue.full()).to_be_true();
		expect(queue.push(noteOn(61, 1000), 1000, guard)).to_be_false();
		queue.pop(guard);
		expect(queue.push(noteOn(61, 1000), 1000, guard)).to_be_true();
	});

	it("pop oldest makes room regardless of due time", _ {
		MIDIQueue queue;
		CriticalSectionGuard guard;
		for (size_t i = 0; i < MIDIQueue::kCapacity; i++) {
			expect(queue.push(noteOn(i % 128, 2000), 1000, guard)).to_be_true();
		}
		expect(queue.push(noteOn(127, 2000), 1000, guard)).to_be_false();

		// None are due, but the oldest still comes out
		expect(queue.frontDue(1000, false, guard)).to_be_null();
		QueuedMIDI oldest;
		expect(queue.popOldest(oldest, guard)).to_be_true();
		expect(oldest.message.data1).to_equal(0);
		expect(queue.push(noteOn(127, 2000), 1000, guard)).to_be_true();

		// The new one went in behind everything already queued
		for (size_t i = 1; i < MIDIQueue::kCapacity; i++) {
			expect(queue.popOldest(oldest, guard)).to_be_true();
			expect(oldest.message.data1).to_equal(i % 128);
		}
		expect(queue.popOldest(oldest, guard)).to_be_true();
		expect(oldest.message.data1).to_equal(127);
		expect(queue.popOldest(oldest, guard)).to_be_false();
	});
});

describe output_time_group("output time", ${
	it("window start is when the playhead reaches it", _ {
		// 441 samples is 10ms
		expect(outputTimeAtWindowOffset(1000, 441, 0)).to_equal(1000u + 10'000u);
	});

	it("offsets within the window are later by sample count", _ {
		OutputTime start = outputTimeAtWindowOffset(5000, 128, 0);
		OutputTime later = outputTimeAtWindowOffset(5000, 128, 441);
		// 441 samples later is 10ms, give or take the Q8 rounding of a sample's length
		expect(static_cast<int32_t>(later - start)).to_be_within(10).of(10'000);
	});

	it("never goes backwards across a window", _ {
		OutputTime previous = outputTimeAtWindowOffset(0, 64, 0);
		for (int32_t offset = 1; offset <= 128; offset++) {
			OutputTime next = outputTimeAtWindowOffset(0, 64, offset);
			expect(static_cast<int32_t>(next - previous)).not_().to_be_less_than(0);
			previous = next;
		}
	});
});

DELUGE_SPEC(midi_queue_spec, midi_queue_group, output_time_group)
