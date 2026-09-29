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

enum class OscType : uint8_t; // opaque (definitions_cxx.hpp); only INPUT_L/INPUT_R/INPUT_STEREO reach the port

namespace deluge::synth {

class LiveInputBuffer; // the per-channel ingest+analysis buffer; host-pooled

/// @brief The port through which the live-input source reads the host's audio input — the dependency-inversion seam
/// that lets the live-input DSP (the pitch-shifter, the ingest/analysis buffer) live in the library without naming
/// `AudioEngine`.
///
/// The firmware supplies the adapter (`AudioInputAdapter`, wrapping `AudioEngine`'s input ring + clock + plug-state +
/// the shared per-channel `LiveInputBuffer` pool); a host/test could supply a fake. See
/// `docs/dev/synth_source_ports.md`. The input ring is interleaved stereo (`NUM_MONO_INPUT_CHANNELS`-wide frames);
/// the per-channel `LiveInputBuffer` is owned by the host pool (shared across voices), so the port hands back a
/// borrowed pointer rather than transferring ownership.
class AudioInputPort {
public:
	virtual ~AudioInputPort() = default;

	/// The monotonic audio frame clock (`AudioEngine::audioSampleTimer`) — the ingest idempotency/staleness key.
	[[nodiscard]] virtual uint32_t sampleTime() const = 0;

	/// Is a stereo external input live? (`micPluggedIn || lineInPluggedIn`) — drives the STEREO→L fallback and the
	/// `INPUT_R` channel offset, re-read per block so mid-note plug/unplug behaves as before.
	[[nodiscard]] virtual bool stereoInputAvailable() const = 0;

	/// The live-input ring read cursor: the first of the current block's interleaved-stereo frames. Step by
	/// `NUM_MONO_INPUT_CHANNELS` and wrap at @ref ringEnd (the host owns the ring + its fill/advance).
	[[nodiscard]] virtual const int32_t* ringReadPos() const = 0;
	/// One past the last sample of the input ring (the wrap bound for @ref ringReadPos).
	[[nodiscard]] virtual const int32_t* ringEnd() const = 0;

	/// The shared, per-channel ingest+analysis buffer for @p inputType (host-pooled across voices, GC'd by
	/// staleness). @return the borrowed buffer, or nullptr if unavailable and @p mayCreate is false / the pool is
	/// empty.
	[[nodiscard]] virtual LiveInputBuffer* bufferForChannel(OscType inputType, bool mayCreate) = 0;

	/// Report the time-stretch hops the pitch-shifter performed this block, for the host's per-routine CPU-load
	/// accounting (the pitch-shifter itself stays free of that global; see LivePitchShifter::render's return value).
	virtual void reportHops(int32_t hopsEnded) = 0;
};

} // namespace deluge::synth
