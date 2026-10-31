/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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

#include "synth/sources/sample/sample_playback_port.h" // SamplePlaybackPort
#include "synth/sources/source.h"                      // deluge::synth::Source, SourceBlock, SourceRenderResult
#include <cstdint>

/// @brief The sample-playback source (OscType::SAMPLE) for one voice-source — the library-resident render
/// orchestration of @ref deluge::synth::Source.
///
/// Drives one pooled voice-sample reader through a @ref SamplePlaybackPort: derives this block's
/// pitch + time-stretch (incl. sequence-sync), decides + sets up the resampling cache, handles the mid-note late
/// start, calls the port's render, and folds the result into the voice's oscBuffer (incl. the stereo-unison
/// mix-down). All the app-coupled machinery — the `VoiceSample` reader, the SD cluster cache, time-stretch, the
/// pool, storage, transport — lives App-side behind the port (@ref SamplePlaybackAdapter); this class names none
/// of it. Zone changes, clones and the cursor read-out are the adapter's own API.
///
/// @note Per the L1 reframe this is a **source component** (usable inside Subtractive/RingMod), not a swappable
/// engine — see `docs/dev/synth_l1_engine_architecture.md`.
namespace deluge::synth {

class SampleSource : public Source {
public:
	explicit SampleSource(SamplePlaybackPort* port) : port_(port) {}

	/// The port this source plays through.
	[[nodiscard]] SamplePlaybackPort* port() const { return port_; }
	/// Plays through @p port, which must outlive this source.
	void bind_port(SamplePlaybackPort* port) { port_ = port; }
	/// Starts the port's reader for the note, @c note.samples_late into it. @return false if it cannot play.
	bool note_on(const SourceNote& note) override { return port_->note_on(note.samples_late, note.priority_rating); }
	/// The note-off: a sample with loop points plays on past its loop end. @return false when it ends now.
	bool release(const SourceRelease& release) override { return port_->note_off_at_loop_end(release.priority_rating); }
	/// Releases the port's reader, if it holds one.
	void unassign(bool deleting_song) override { port_->unassign(deleting_song); }

	/// Renders one block of sample playback into @c block.osc_buffer. Reads the voice's per-block values in
	/// @c block.sample (porta, priority, the pre-modulation phase increment, the caching pitch-mod flag); the
	/// resampling, time-stretch and cache machinery and the note's source and guide live behind the port.
	SourceRenderResult render(const SourceBlock& block) override;

private:
	/// Derive this block's pitch (@p phaseIncrement, mutated in place) + @p timeStretchRatio (+ @p noteLengthInSamples
	/// when syncing) from the port. @return false ⇒ unassign (sync would exceed the 32× speed limit).
	bool get_pitch_and_speed_params(uint32_t* phase_increment, uint32_t* time_stretch_ratio,
	                                uint32_t* note_length_in_samples);
	/// The non-syncing time-stretch ratio: counteract pitch when pitch/speed are independent, then apply the manual
	/// time-stretch amount. Also used for the late-start position (with the non-modulated phaseIncrement).
	uint32_t get_speed_param_for_no_syncing(int32_t phase_increment, int32_t pitch_adjust_neutral_value);

	SamplePlaybackPort* port_;
};

} // namespace deluge::synth
