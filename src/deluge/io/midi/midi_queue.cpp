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

int32_t samplesTilWindowOffsetPlays(int32_t samplesBeforeLappingPlayHead, int32_t offset) {
	// Offsets within the free space play once the playhead laps round to them. The window can be longer than that -
	// later offsets are only written once the playhead has passed them, so they play on its following lap. Both come
	// out the same
	return offset - samplesBeforeLappingPlayHead + SSI_TX_BUFFER_NUM_SAMPLES;
}

uint32_t dueAtWindowOffset(uint32_t windowStartTicks, int32_t samplesBeforeLappingPlayHead, int32_t offset) {
	uint32_t samplesTilDue = samplesTilWindowOffsetPlays(samplesBeforeLappingPlayHead, offset);
	return windowStartTicks + ((samplesTilDue * kSystemTicksPerSampleQ8) >> 8);
}

bool MIDIQueue::push(MIDIMessage message, int32_t filter, bool usb, bool din, uint32_t due, uint32_t now) {
	int32_t ticksAhead = (int32_t)(due - now);
	if (ticksAhead < 0 || ticksAhead > kMaxTicksAhead) {
		due = now;
	}
	return queue_.push(QueuedMIDI{.message = message, .filter = (int16_t)filter, .usb = usb, .din = din, .due = due});
}

bool MIDIQueue::popDue(QueuedMIDI& out, uint32_t now, bool all) {
	if (!queue_.front(out) || !(all || (int32_t)(out.due - now) <= 0)) {
		return false;
	}
	queue_.pop();
	return true;
}

bool MIDIQueue::popOldest(QueuedMIDI& out) {
	return queue_.pop(out);
}

bool MIDIQueue::nextDue(uint32_t& due) {
	QueuedMIDI queued;
	if (!queue_.front(queued)) {
		return false;
	}
	due = queued.due;
	return true;
}
