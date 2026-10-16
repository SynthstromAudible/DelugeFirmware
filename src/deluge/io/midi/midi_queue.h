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

#include "OSLikeStuff/timers_interrupts/clock_type.h"
#include "definitions_cxx.hpp"
#include "model/midi/message.h"
#include <cstddef>
#include <cstdint>
#include <etl/queue_spsc_atomic.h>

/// System timer ticks (see getSystemTicks()) per audio sample, in Q8 fixed point
constexpr uint32_t kSystemTicksPerSampleQ8 = ((uint64_t)DELUGE_CLOCKS_PER << 8) / kSampleRate;

/// Samples from the start of a render window until the playhead plays offset in it, given how many samples the playhead
/// was then from lapping the start of the window
int32_t samplesTilWindowOffsetPlays(int32_t samplesBeforeLappingPlayHead, int32_t offset);

/// Time the playhead reaches offset in a render window, given the system timer time at the start of the window and how
/// many samples the playhead was then from lapping the start of the window
uint32_t dueAtWindowOffset(uint32_t windowStartTicks, int32_t samplesBeforeLappingPlayHead, int32_t offset);

struct QueuedMIDI {
	MIDIMessage message;
	int16_t filter;
	bool usb;
	bool din;
	uint32_t due; ///< system timer ticks, see getSystemTicks()
};
static_assert(sizeof(QueuedMIDI) == 12);

/// MIDI waiting to be sent at its due time. Lock free for a single producer (the audio routine) and a single consumer
/// (the MIDI/gate output timer ISR)
class MIDIQueue {
public:
	static constexpr size_t kCapacity = 256;
	/// Due times are never legitimately more than an audio buffer or so ahead
	static constexpr int32_t kMaxTicksAhead = DELUGE_CLOCKS_PER / 100;

	/// Producer only. A due time in the past or more than kMaxTicksAhead after now is stale (e.g. the audio routine
	/// hasn't run for a while) and is replaced with now, rather than letting the wrap make it look far in the future
	/// and hold up the queue. Returns false if the queue's full
	bool push(MIDIMessage message, int32_t filter, bool usb, bool din, uint32_t due, uint32_t now);

	/// Consumer only. Take the oldest queued MIDI if it's due by now, or regardless if all. MIDI comes out in the order
	/// it was queued, so anything due behind something which isn't yet waits for it
	bool popDue(QueuedMIDI& out, uint32_t now, bool all = false);

	/// Consumer only (or the producer, with the consumer locked out). Take the oldest queued MIDI whether it's due or
	/// not, e.g. to make room when the queue's full. Returns false if nothing's queued
	bool popOldest(QueuedMIDI& out);

	/// Consumer only. Get the due time of the oldest queued MIDI. Returns false if nothing's queued
	bool nextDue(uint32_t& due);

	[[nodiscard]] bool empty() const { return queue_.empty(); }
	[[nodiscard]] bool full() const { return queue_.full(); }
	[[nodiscard]] size_t size() const { return queue_.size(); }

private:
	etl::queue_spsc_atomic<QueuedMIDI, kCapacity> queue_;
};
