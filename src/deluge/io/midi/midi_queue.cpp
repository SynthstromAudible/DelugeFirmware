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

#include "io/midi/midi_queue.h"
#include <libdeluge/clock.h>

OutputTime outputTimeNow() {
	return static_cast<OutputTime>(deluge_clock_now());
}

OutputTime outputTimeAtWindowOffset(OutputTime windowStart, int32_t samplesUntilWindowPlays, int32_t offset) {
	uint32_t samplesUntilDue = static_cast<uint32_t>(samplesUntilWindowPlays + offset);
	return windowStart + ((samplesUntilDue * kMicrosPerSampleQ8) >> 8);
}

bool MIDIQueue::push(const QueuedMIDI& queued, OutputTime now, const CriticalSectionGuard&) {
	if (queue_.full()) {
		return false;
	}
	QueuedMIDI entry = queued;
	int32_t microsAhead = static_cast<int32_t>(entry.due - now);
	if (microsAhead < 0 || microsAhead > kMaxMicrosAhead) {
		entry.due = now;
	}
	queue_.push(entry);
	return true;
}

const QueuedMIDI* MIDIQueue::frontDue(OutputTime now, bool all, const CriticalSectionGuard&) const {
	if (queue_.empty()) {
		return nullptr;
	}
	const QueuedMIDI& front = queue_.front();
	if (!all && static_cast<int32_t>(front.due - now) > 0) {
		return nullptr;
	}
	return &front;
}

bool MIDIQueue::popOldest(QueuedMIDI& out, const CriticalSectionGuard&) {
	if (queue_.empty()) {
		return false;
	}
	out = queue_.front();
	queue_.pop();
	return true;
}

bool MIDIQueue::nextDue(OutputTime& due, const CriticalSectionGuard&) const {
	if (queue_.empty()) {
		return false;
	}
	due = queue_.front().due;
	return true;
}
