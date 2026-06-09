/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// audio_io.cpp — the RZ/A1L render driver behind <libdeluge/audio_io.h>.
///
/// Owns everything about how rendered samples reach the DAC and how received
/// samples reach the application: the SSI TX/RX DMA rings (always accessed
/// through their uncached mirror — see getTxBufferStart(); a cached pointer
/// here causes intermittent DMA corruption), the write/read cursors, the ring
/// wraparound, the RX latency window, and the DMA-pacing policy that chooses
/// each render-window length (audio_pacing.h). Relocated verbatim from
/// AudioEngine::routine_() / doSomeOutputting() / slowRoutine() per
/// docs/dev/audio_callback_relocation.md.
///
/// The application side of the boundary is deluge_app_render(in, out, frames):
/// it renders exactly `frames` stereo samples into `out` (this driver's
/// scratch block) reading `frames` aligned input samples from `in`; this
/// driver then flushes the scratch into the TX ring as DMA space frees,
/// possibly across several deluge_audio_drive() calls — preserving the old
/// behaviour where a doubled render window could exceed the currently-free
/// ring space and trickle out over subsequent routine calls.

#include "audio_pacing.h"
#include "board_config.h"
#include <cstring>

extern "C" {
#include "drivers/ssi/ssi.h"
#include "libdeluge/app.h"
#include "libdeluge/audio_io.h"
}

namespace {

// TX: write cursor (uncached byte address into the TX ring) and the cached DMA
// play-head poll ("saddr" in the old audio_engine.cpp code).
uint32_t txWritePos;
uint32_t playPosCached;

// RX: read cursor (uncached byte address into the RX ring), kept one latency
// window behind the DMA write head.
uint32_t rxReadPos;

// The render block. The application renders into `renderBlock` (out) and reads
// `inputBlock` (in); [pendingPos, pendingEnd) is rendered but not yet flushed.
DelugeStereoSample renderBlock[SSI_TX_BUFFER_NUM_SAMPLES];
DelugeStereoSample inputBlock[SSI_TX_BUFFER_NUM_SAMPLES];
uint32_t pendingPos;
uint32_t pendingEnd;

// Play/write anchors of the current render block, for sample-accurate
// gate/MIDI scheduling (deluge_audio_frames_until_block_offset).
uint32_t blockStartWriteFrame;
uint32_t blockStartPlayFrame;

constexpr uint32_t kTxMask = SSI_TX_BUFFER_NUM_SAMPLES - 1;
constexpr uint32_t kOutShift = 2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE;
constexpr uint32_t kInShift = 2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE;

static_assert(sizeof(DelugeStereoSample) == (1u << kOutShift), "stereo frame stride mismatch");

// Flush pending rendered samples into the TX ring, stopping (false) when the
// write cursor would lap the play head. Re-polls the play head once when it
// appears full, exactly as the old doSomeOutputting() did.
bool flushPending() {
	int32_t* __restrict__ write = (int32_t*)txWritePos;

	while (pendingPos != pendingEnd) {

		// If we've reached the end of the known space in the output buffer...
		if (!(((uint32_t)((uint32_t)write - playPosCached) >> kOutShift) & kTxMask)) {

			// See if there's now some more space.
			playPosCached = (uint32_t)getTxBufferCurrentPlace();

			// If there wasn't, stop for now
			if (!(((uint32_t)((uint32_t)write - playPosCached) >> kOutShift) & kTxMask)) {
				txWritePos = (uint32_t)write;
				return false;
			}
		}

		write[0] = renderBlock[pendingPos].l;
		write[1] = renderBlock[pendingPos].r;

		write += NUM_MONO_OUTPUT_CHANNELS;
		if (write == getTxBufferEnd()) {
			write = getTxBufferStart();
		}
		pendingPos++;
	}

	txWritePos = (uint32_t)write;
	return true;
}

// Copy the next `frames` received samples out of the RX ring (uncached) into
// the contiguous input block, and advance the read cursor.
void prepareInputBlock(uint32_t frames) {
	uint32_t ringBytes = SSI_RX_BUFFER_NUM_SAMPLES << kInShift;
	uint32_t copyBytes = frames << kInShift;
	uint32_t bytesToEnd = (uint32_t)getRxBufferEnd() - rxReadPos;
	uint32_t firstChunkBytes = (copyBytes < bytesToEnd) ? copyBytes : bytesToEnd;

	memcpy(inputBlock, (void*)rxReadPos, firstChunkBytes);
	if (copyBytes != firstChunkBytes) {
		memcpy((uint8_t*)inputBlock + firstChunkBytes, getRxBufferStart(), copyBytes - firstChunkBytes);
	}

	rxReadPos += copyBytes;
	if (rxReadPos >= (uint32_t)getRxBufferEnd()) {
		rxReadPos -= ringBytes;
	}
}

} // namespace

extern "C" {

DelugeStatus deluge_audio_start(void) {
	// The SSI/DMA stream itself free-runs from board init (ssiInit()); this
	// sets the cursors. Idempotent in effect: calling again just re-anchors.
	txWritePos = (uint32_t)getTxBufferStart();

	rxReadPos = (uint32_t)getRxBufferStart()
	            + ((SSI_RX_BUFFER_NUM_SAMPLES - SSI_TX_BUFFER_NUM_SAMPLES - 16)
	               << kInShift); // Subtracting 5 or more seems fine

	playPosCached = (uint32_t)getTxBufferCurrentPlace();
	pendingPos = pendingEnd = 0;
	return DELUGE_OK;
}

void deluge_audio_stop(void) {
	// The free-running ring keeps the codec alive; nothing to do on this board.
}

uint32_t deluge_audio_max_block_frames(void) {
	return SSI_TX_BUFFER_NUM_SAMPLES;
}

uint32_t deluge_audio_sample_rate(void) {
	return 44100;
}

uint32_t deluge_audio_output_latency_frames(void) {
	// The app renders up to one TX ring ahead of the play head.
	return SSI_TX_BUFFER_NUM_SAMPLES;
}

uint32_t deluge_audio_drive(void) {
	uint32_t renders = 0;

	while (flushPending() && renders < 2) {

		playPosCached = (uint32_t)getTxBufferCurrentPlace();
		uint32_t rawFrames = ((uint32_t)(playPosCached - txWritePos) >> kOutShift) & kTxMask;

		// Render only as far as the DMA play head has advanced; on a second
		// pass within one drive, only if it's been long enough to be worth it.
		if (rawFrames <= (10 * renders)) {
			break;
		}

		uint32_t frames = deluge_rza1_pace_window(rawFrames, SSI_TX_BUFFER_NUM_SAMPLES);

		blockStartWriteFrame = txWritePos >> kOutShift;
		blockStartPlayFrame = playPosCached >> kOutShift;

		prepareInputBlock(frames);

		deluge_app_render(inputBlock, renderBlock, frames);

		pendingPos = 0;
		pendingEnd = frames;
		renders++;
	}

	return renders;
}

void deluge_audio_input_resync(void) {
	// The RX buffer is much bigger than the TX, and we get our timing from the TX buffer's sending.
	// However, if there's an audio glitch, and also at start-up, it's possible we might have missed an entire cycle
	// of the TX buffer. That would cause the RX buffer's latency to increase. So here, we check for that and
	// correct it. The correct latency for the RX buffer is between (SSI_TX_BUFFER_NUM_SAMPLES) and (2 *
	// SSI_TX_BUFFER_NUM_SAMPLES).

	uint32_t rxBufferWriteAddr = (uint32_t)getRxBufferCurrentPlace();
	uint32_t latencyWithinAppropriateWindow =
	    (((rxBufferWriteAddr - rxReadPos) >> kInShift) - SSI_TX_BUFFER_NUM_SAMPLES) & (SSI_RX_BUFFER_NUM_SAMPLES - 1);

	while (latencyWithinAppropriateWindow >= SSI_TX_BUFFER_NUM_SAMPLES) {
		rxReadPos += (SSI_TX_BUFFER_NUM_SAMPLES << kInShift);
		if (rxReadPos >= (uint32_t)getRxBufferEnd()) {
			rxReadPos -= (SSI_RX_BUFFER_NUM_SAMPLES << kInShift);
		}
		latencyWithinAppropriateWindow = (((rxBufferWriteAddr - rxReadPos) >> kInShift) - SSI_TX_BUFFER_NUM_SAMPLES)
		                                 & (SSI_RX_BUFFER_NUM_SAMPLES - 1);
	}
}

uint32_t deluge_audio_frames_until_block_offset(uint32_t offset_frames, uint32_t* elapsed_frames_out) {
	uint32_t playNowFrame = (uint32_t)getTxBufferCurrentPlace() >> kOutShift;
	if (elapsed_frames_out != nullptr) {
		*elapsed_frames_out = playNowFrame - blockStartPlayFrame;
	}
	return (blockStartWriteFrame + offset_frames - playNowFrame) & kTxMask;
}

uint32_t deluge_audio_stamp_to_render_offset(uint32_t stamp) {
	return ((uint32_t)(stamp - txWritePos) >> kOutShift) & kTxMask;
}

} // extern "C"
