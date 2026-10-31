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

#include "definitions_cxx.hpp"                         // LoopType / MarkerType
#include "synth/sources/sample/sample_playback_port.h" // synth::SamplePlaybackPort
#include <cstdint>

class Sample;
class Source;
class VoiceSample;
class VoiceSamplePlaybackGuide;

/// @brief The firmware adapter behind @ref deluge::synth::SamplePlaybackPort: one pooled @ref VoiceSample reader
/// for one voice-source. Holds the source's `VoiceSample` + the per-note `::Source`/guide cached at note-on,
/// soliciting/releasing from the `AudioEngine` voice-sample pool and answering the library `SampleSource`'s render
/// queries/ops from that state. This is the app-coupled half the library-resident `SampleSource` no longer
/// carries (storage, the SD cluster cache, time-stretch, the pool, `playbackHandler` all stay here).
///
/// It also owns the note **lifecycle** (solicit/release, note-off-at-loop-end, zone change, the GUI play-cursor
/// read-out, the unison-count-change clone) — app-typed methods called directly by the App, deliberately off the
/// port (the library shell never drives them). Ownership of the pooled `VoiceSample` is **explicit** (released in
/// @ref unassign, never in a destructor) — the per-source objects are pooled and reused in place.
class SamplePlaybackAdapter final : public deluge::synth::SamplePlaybackPort {
public:
	// --- App-side note lifecycle (called directly by the App, not via the port) ------------------------------

	/// The note's source and playback guide, which the port's note-on and render read: set before each note-on.
	void bind(::Source* source, VoiceSamplePlaybackGuide* guide) {
		source_ = source;
		guide_ = guide;
	}

	/// Free the pooled VoiceSample back to AudioEngine (no-op if none held); clear the cached source/guide.
	void unassign(bool deletingSong) override;

	/// React to a sample zone/marker change. Returns false if this source is now out of bounds and should be
	/// unassigned.
	bool zoneChanged(VoiceSamplePlaybackGuide* guide, Sample* sample, bool reversed, MarkerType markerType,
	                 LoopType loopingType, int32_t priorityRating);

	/// The wrapped low-level reader, or nullptr if none is held (GUI play-cursor read-out + the clone below).
	[[nodiscard]] VoiceSample* voiceSample() const { return voiceSample_; }

	/// Unison-count increase: solicit a fresh VoiceSample for this (new) part, clone @p from's low-level reader state
	/// into it, and play @p from's source through its guide. Returns false if none could be solicited.
	bool cloneVoiceSampleFrom(const SamplePlaybackAdapter& from);

	// --- deluge::synth::SamplePlaybackPort (primitive queries/ops the library SampleSource drives) ------------
	[[nodiscard]] int32_t num_channels() const override;
	[[nodiscard]] int32_t sample_rate() const override;
	[[nodiscard]] int32_t neutral_phase_increment() const override;
	[[nodiscard]] int32_t sample_length_at_system_sample_rate() const override;
	[[nodiscard]] bool is_stretch_repeat_mode() const override;
	[[nodiscard]] bool pitch_and_speed_independent() const override;
	[[nodiscard]] int32_t time_stretch_amount() const override;
	[[nodiscard]] uint32_t sequence_sync_length_ticks() const override;
	[[nodiscard]] LoopType looping_type() const override;
	[[nodiscard]] LoopPoints loop_points() const override;
	[[nodiscard]] uint64_t time_per_internal_tick_big() const override;
	[[nodiscard]] int64_t synced_num_samples_in() const override;
	[[nodiscard]] int32_t num_samples_lagging_behind_sync() const override;
	[[nodiscard]] uint32_t adjust_pitch_to_correct_drift_from_sync(uint32_t phaseIncrement) const override;
	[[nodiscard]] int32_t interpolation_buffer_size(uint32_t phaseIncrement) const override;
	[[nodiscard]] uint32_t pending_samples_late() const override;
	[[nodiscard]] bool done_first_render_yet() const override;
	/// Solicits (or restarts in place) the pooled VoiceSample and sets up its clusters for initial play. Returns false
	/// only on a hard failure that should silence the whole Voice; true otherwise (including an unplayable sample and
	/// a late start).
	bool note_on(uint32_t samples_late, int32_t priority_rating) override;
	bool note_off_at_loop_end(int32_t priority_rating) override;
	[[nodiscard]] int32_t bytes_left_to_end() const override;
	[[nodiscard]] int32_t bytes_per_frame() const override;
	LateStartOutcome attempt_late_start(int64_t rawSamplesLate, int32_t numSamples) override;
	bool possibly_set_up_cache(int32_t phaseIncrement, int32_t timeStretchRatio, int32_t priorityRating,
	                           LoopType loopingType) override;
	bool render(int32_t* buffer, int32_t numSamples, LoopType loopingType, int32_t phaseIncrement,
	            int32_t timeStretchRatio, int32_t sourceAmplitude, int32_t amplitudeIncrement,
	            int32_t interpolationBufferSize, int32_t priorityRating) override;

private:
	VoiceSample* voiceSample_ = nullptr;
	/// The per-note source config + playback guide, cached at note-on (stable for the note; `sound.sources[s]` /
	/// `voice.guides[s]` are fixed members). The render queries above read these. Cleared in @ref unassign.
	::Source* source_ = nullptr;
	VoiceSamplePlaybackGuide* guide_ = nullptr;
};
