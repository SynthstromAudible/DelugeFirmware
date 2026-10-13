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

#include "definitions_cxx.hpp"
#include "midi/message.h"
#include <cstddef>
#include <cstdint>
#include <etl/queue.h>

struct CriticalSectionGuard;

/// Output times are microseconds on the deluge_clock_now() clock, truncated to 32 bits. That wraps every 71 minutes,
/// so they are only ever compared by signed difference, never by magnitude.
using OutputTime = uint32_t;

/// The low 32 bits of deluge_clock_now(): the clock output times are kept on.
OutputTime outputTimeNow();

/// Microseconds per audio sample, in Q8 fixed point
constexpr uint32_t kMicrosPerSampleQ8 = ((1'000'000u << 8) + kSampleRate / 2) / kSampleRate;

/// The time the playhead reaches `offset` samples into a render window, given the time the window began rendering and
/// how many samples from then until the playhead played the window's first sample
OutputTime outputTimeAtWindowOffset(OutputTime windowStart, int32_t samplesUntilWindowPlays, int32_t offset);

struct QueuedMIDI {
	MIDIMessage message;
	int16_t filter;
	bool usb;
	bool din;
	OutputTime due;
};
static_assert(sizeof(QueuedMIDI) == 12);

/// MIDI waiting to be sent at its due time. Filled from the interaction and audio tiers and drained by the MIDI/gate
/// output timer's interrupt, so every operation takes a CriticalSectionGuard: the caller holds it across a whole
/// push or drain.
class MIDIQueue {
public:
	static constexpr size_t kCapacity = 256;
	/// Due times are never legitimately more than an audio buffer or so ahead
	static constexpr int32_t kMaxMicrosAhead = 10'000;

	/// A due time in the past or more than kMaxMicrosAhead after now is stale (e.g. the audio render hasn't run for a
	/// while) and is replaced with now, rather than letting the wrap make it look far in the future and hold up the
	/// queue. Returns false if the queue's full
	bool push(const QueuedMIDI& queued, OutputTime now, const CriticalSectionGuard&);

	/// The oldest queued MIDI, if it's due by now or regardless if all. MIDI comes out in the order it was queued, so
	/// anything due behind something which isn't yet waits for it. Doesn't remove it: see pop()
	[[nodiscard]] const QueuedMIDI* frontDue(OutputTime now, bool all, const CriticalSectionGuard&) const;

	/// Remove the oldest queued MIDI
	void pop(const CriticalSectionGuard&) { queue_.pop(); }

	/// Get the due time of the oldest queued MIDI. Returns false if nothing's queued
	bool nextDue(OutputTime& due, const CriticalSectionGuard&) const;

	[[nodiscard]] bool empty() const { return queue_.empty(); }
	[[nodiscard]] bool full() const { return queue_.full(); }
	[[nodiscard]] size_t size() const { return queue_.size(); }

private:
	etl::queue<QueuedMIDI, kCapacity> queue_;
};
