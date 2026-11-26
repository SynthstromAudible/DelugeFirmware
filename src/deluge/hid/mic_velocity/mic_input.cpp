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

#include "hid/mic_velocity/mic_input.h"
#include "OSLikeStuff/scheduler_api.h"
#include "OSLikeStuff/timers_interrupts/clock_type.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include <algorithm>
#include <array>

extern "C" {
#include "drivers/ssi/ssi.h"
}

namespace deluge::mic_velocity::input {

namespace {

constexpr int32_t kRing = SSI_RX_BUFFER_NUM_SAMPLES; // Frames
static_assert((kRing & (kRing - 1)) == 0);
constexpr int32_t kTxRing = SSI_TX_BUFFER_NUM_SAMPLES;
/// The reader keeps this far clear of the DMA's write point when it has fallen behind, so the DMA doesn't overwrite
/// frames as they're read
constexpr int32_t kOverrunMargin = 256;

// The clock
Frame clockFrame = 0;      // The frame the DMA was writing at the last now()
int32_t clockDmaIndex = 0; // Where in the ring that was
uint32_t clockTicks = 0;   // getSystemTicks() then
bool clockStarted = false;

// The reader
Consumer consumer = nullptr;
Frame readFrame = 0;
uint32_t dropped = 0;
bool inService = false;

// The output sent to the codec, by the frame it plays at: mono, 16 bits. Twice the input ring, so a frame's output
// (written up to a TX ring ahead) is still there when the reader gets to it up to a ring later.
constexpr int32_t kRefRing = kRing * 2;
PLACE_SDRAM_BSS std::array<int16_t, kRefRing> refRing;

int32_t dmaIndex() {
	auto const* const place = static_cast<int32_t const*>(getRxBufferCurrentPlace());
	return static_cast<int32_t>(place - getRxBufferStart()) / NUM_MONO_INPUT_CHANNELS;
}

} // namespace

Frame now() {
	int32_t const index = dmaIndex();
	uint32_t const ticks = getSystemTicks();
	if (!clockStarted) {
		clockStarted = true;
		clockDmaIndex = index;
		clockTicks = ticks;
		// A frame's number modulo the ring is where it sits in the ring. Starting a lap in keeps frames from before
		// the start positive.
		clockFrame = static_cast<Frame>(kRing + index);
		return clockFrame;
	}
	uint32_t moved = static_cast<uint32_t>(index - clockDmaIndex) & (kRing - 1);
	// The ring index alone can't see whole laps. If more than half a ring's time has passed, count the laps from the
	// system timer.
	uint32_t const elapsedTicks = ticks - clockTicks;
	uint64_t const elapsedFrames = static_cast<uint64_t>(elapsedTicks) * kSampleRate / DELUGE_CLOCKS_PER;
	if (elapsedFrames > static_cast<uint64_t>(kRing / 2)) {
		int64_t const laps = (static_cast<int64_t>(elapsedFrames) - moved + kRing / 2) / kRing;
		if (laps > 0) {
			moved += static_cast<uint32_t>(laps) * kRing;
		}
	}
	clockFrame += moved;
	clockDmaIndex = index;
	clockTicks = ticks;
	return clockFrame;
}

void setConsumer(Consumer newConsumer) {
	consumer = newConsumer;
	readFrame = now();
	refRing.fill(0);
}

bool hasConsumer() {
	return consumer != nullptr;
}

uint32_t framesDropped() {
	return dropped;
}

void service() {
	if (consumer == nullptr || inService) {
		return;
	}
	inService = true;
	Frame const until = now();
	int32_t behind = static_cast<int32_t>(until - readFrame);
	if (behind > kRing - kOverrunMargin) {
		Frame const resume = until - (kRing - kOverrunMargin);
		dropped += resume - readFrame;
		readFrame = resume;
		behind = static_cast<int32_t>(until - readFrame);
	}

	int32_t const* const ring = getRxBufferStart();
	std::array<int32_t, kMaxBlock> mic;
	std::array<int16_t, kMaxBlock> ref;
	while (behind > 0) {
		int32_t const n = std::min<int32_t>(behind, kMaxBlock);
		for (int32_t i = 0; i < n; i++) {
			Frame const f = readFrame + i;
			mic[i] = ring[(f & (kRing - 1)) * NUM_MONO_INPUT_CHANNELS]; // Left: the internal mic
			int16_t& r = refRing[f & (kRefRing - 1)];
			ref[i] = r;
			r = 0; // So a frame the engine didn't output (it fell behind) reads as silence next time round
		}
		consumer(readFrame, std::span{mic}.first(n), std::span{ref}.first(n));
		readFrame += n;
		behind -= n;
	}
	inService = false;
}

void feedOutput(std::span<StereoSample const> written, int32_t framesQueuedAfter) {
	if (consumer == nullptr || written.empty() || framesQueuedAfter < 0 || framesQueuedAfter > kTxRing) {
		return;
	}
	// The write position equals the DMA's both when the ring is empty and when it's full. It holds at least what was
	// just written, so a smaller count means full.
	if (framesQueuedAfter < static_cast<int32_t>(written.size())) {
		framesQueuedAfter += kTxRing;
	}
	// The DMA input and output run off the same clock, so the first frame written plays (and the mic hears it, give or
	// take the codec's delays) this many frames from now
	Frame const first = now() + framesQueuedAfter - static_cast<int32_t>(written.size());
	for (size_t i = 0; i < written.size(); i++) {
		int32_t const mono = (written[i].l >> 1) + (written[i].r >> 1);
		refRing[(first + i) & (kRefRing - 1)] = static_cast<int16_t>(mono >> 16);
	}
}

} // namespace deluge::mic_velocity::input
