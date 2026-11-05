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

#include <cstdint>

namespace deluge::synth {

/// @brief The port through which the live-input source reads the host's audio input: the frame clock, the codec ring,
/// the plug state and the hop report, so the live-input DSP lives in the library without naming `AudioEngine`.
///
/// The firmware supplies the adapter (`AudioInputAdapter`); a host or test can supply a fake. See
/// `docs/dev/synth_source_ports.md`. The input ring is interleaved stereo (`NUM_MONO_INPUT_CHANNELS`-wide frames). The
/// histories the shifter reads are the oscillator's own (@ref LiveInputs), not the host's.
class AudioInputPort {
public:
	virtual ~AudioInputPort() = default;

	/// The monotonic audio frame clock (`AudioEngine::audioSampleTimer`) — the ingest idempotency/staleness key.
	[[nodiscard]] virtual uint32_t sample_time() const = 0;

	/// Is a stereo external input live? (`micPluggedIn || lineInPluggedIn`) — drives the STEREO→L fallback and the
	/// `INPUT_R` channel offset, re-read per block so mid-note plug/unplug behaves as before.
	[[nodiscard]] virtual bool stereo_input_available() const = 0;

	/// The live-input ring read cursor: the first of the current block's interleaved-stereo frames. Step by
	/// `NUM_MONO_INPUT_CHANNELS` and wrap at @ref ringEnd (the host owns the ring + its fill/advance).
	[[nodiscard]] virtual const int32_t* ring_read_pos() const = 0;
	/// One past the last sample of the input ring (the wrap bound for @ref ringReadPos).
	[[nodiscard]] virtual const int32_t* ring_end() const = 0;

	/// Report the time-stretch hops the pitch-shifter performed this block, for the host's per-routine CPU-load
	/// accounting (the pitch-shifter itself stays free of that global; see PitchShifter::render's return value).
	virtual void report_hops(int32_t hops_ended) = 0;
};

} // namespace deluge::synth
