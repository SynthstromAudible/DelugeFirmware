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

#include "definitions_cxx.hpp" // LoopType
#include <cstdint>

namespace deluge::synth {

/// @brief The port through which a sample source drives one pooled voice-sample reader — the dependency-inversion
/// seam that lets `SampleSource` live in the library without naming the firmware's `VoiceSample`, `Sample`,
/// `SampleHolder`, the playback guide, `playbackHandler`, or `AudioEngine`'s voice-sample pool.
///
/// This is the **widest** of the source ports: sample playback is the most app-coupled source (storage, the SD
/// cluster cache, time-stretch, the pool, transport). Per `docs/dev/synth_source_ports.md` the heavy machinery
/// stays App-side as the adapter (`SamplePlaybackAdapter`, wrapping `VoiceSample` + the pool); only the per-voice
/// **render orchestration** lives in the library `SampleSource`. The adapter caches the note's source/guide/
/// voice-sample, so every method here is a **primitive** query or op — no app types cross the seam. A host/test
/// could supply a fake.
///
/// The note's start, its loop-end note-off and its release are on the port; the rest of the lifecycle (zone change,
/// unison clone, the GUI's play-cursor read-out) is the adapter's own app-typed API, called directly by the App.
class SamplePlaybackPort {
public:
	virtual ~SamplePlaybackPort() = default;

	/// Resolved sample loop bounds (in bytes) for the cache loop-length decision: the loop point if defined, else
	/// the play bound. Derived App-side from the `SampleHolderForVoice`.
	struct LoopPoints {
		int32_t start;
		int32_t end;
	};

	/// The outcome of a late sample start — the library mapping of the firmware's `LateStartAttemptStatus`.
	enum class LateStartOutcome : uint8_t {
		OK,      ///< started fine — carry on rendering this block
		WAIT,    ///< not yet — skip this unison part this block without unassigning
		FAILURE, ///< can't start — unassign this source
	};

	// --- Note-stable facts (constant for the life of the note) -----------------------------------------------
	/// Channel count to render at: 2 for a stereo sample, else 1.
	[[nodiscard]] virtual int32_t num_channels() const = 0;
	/// The sample's native sample rate (used for the sync-drift tolerance).
	[[nodiscard]] virtual int32_t sample_rate() const = 0;
	/// The phase increment that plays the sample back at its natural pitch (the "neutral" pitch). UQ0.32.
	[[nodiscard]] virtual int32_t neutral_phase_increment() const = 0;
	/// The sample length converted to the system sample rate (for time-stretch syncing).
	[[nodiscard]] virtual int32_t sample_length_at_system_sample_rate() const = 0;
	/// True when the preset's repeat mode is STRETCH (the easy late-start path).
	[[nodiscard]] virtual bool is_stretch_repeat_mode() const = 0;
	/// True when pitch and speed are treated independently (time-stretch counteracts pitch).
	[[nodiscard]] virtual bool pitch_and_speed_independent() const = 0;
	/// The preset's manual time-stretch amount (index into the firmware's adjust table).
	[[nodiscard]] virtual int32_t time_stretch_amount() const = 0;
	/// The sequence-sync length in ticks (0 ⇒ not syncing).
	[[nodiscard]] virtual uint32_t sequence_sync_length_ticks() const = 0;
	/// The looping type for this note (NONE / low-level / etc.), from the preset + guide.
	[[nodiscard]] virtual LoopType looping_type() const = 0;
	/// The resolved loop bounds for the cache loop-length check.
	[[nodiscard]] virtual LoopPoints loop_points() const = 0;

	// --- Per-block transport / sync queries ------------------------------------------------------------------
	/// The current time-per-internal-tick (the transport clock; UQ value the sync maths shifts down by 32).
	[[nodiscard]] virtual uint64_t time_per_internal_tick_big() const = 0;
	/// Samples elapsed into the synced region (the easy STRETCH late-start position).
	[[nodiscard]] virtual int64_t synced_num_samples_in() const = 0;
	/// How many samples the play head lags the sync grid (signed; for the small-drift no-stretch shortcut).
	[[nodiscard]] virtual int32_t num_samples_lagging_behind_sync() const = 0;
	/// Nudge @p phaseIncrement to correct drift from the sync grid.
	[[nodiscard]] virtual uint32_t adjust_pitch_to_correct_drift_from_sync(uint32_t phase_increment) const = 0;
	/// The interpolation work-buffer size for @p phaseIncrement at the preset's quality.
	[[nodiscard]] virtual int32_t interpolation_buffer_size(uint32_t phase_increment) const = 0;

	// --- Voice-sample state ----------------------------------------------------------------------------------
	/// Samples the note is waiting to start late by (0 ⇒ not a late start). Sign-magnitude UQ.
	[[nodiscard]] virtual uint32_t pending_samples_late() const = 0;
	/// True once the first render block has run (gates the once-per-note cache setup).
	[[nodiscard]] virtual bool done_first_render_yet() const = 0;
	/// Bytes from the play position to where playback ends, counted in the play direction (for the auto-release).
	[[nodiscard]] virtual int32_t bytes_left_to_end() const = 0;
	/// Bytes per sample frame: the byte depth times the channel count.
	[[nodiscard]] virtual int32_t bytes_per_frame() const = 0;

	// --- Voice-sample ops ------------------------------------------------------------------------------------
	/// Starts this note's playback, @p samples_late into it. @p priority_rating is the voice's in the cull order.
	/// @return false if the voice cannot sound at all; a sample that cannot play is not a failure.
	virtual bool note_on(uint32_t samples_late, int32_t priority_rating) = 0;
	/// The note-off for a sample with a loop end: playback goes on past it. @return false ⇒ unassign this source.
	virtual bool note_off_at_loop_end(int32_t priority_rating) = 0;
	/// Releases the reader the note holds, if any (on @p deleting_song, without touching what the song frees).
	virtual void unassign(bool deleting_song) = 0;
	/// Attempt to begin playback @p rawSamplesLate samples into the sample (a mid-note unmute / late start).
	virtual LateStartOutcome attempt_late_start(int64_t raw_samples_late, int32_t num_samples) = 0;
	/// Set up the resampling cache for this note if the conditions allow. @return false ⇒ unassign this source.
	virtual bool possibly_set_up_cache(int32_t phase_increment, int32_t time_stretch_ratio, int32_t priority_rating,
	                                   LoopType looping_type) = 0;
	/// Render @p numSamples of sample playback (resample / time-stretch / cache) into @p buffer at the given pitch
	/// and stretch. @return false ⇒ the sample ended ⇒ unassign this source.
	virtual bool render(int32_t* buffer, int32_t num_samples, LoopType looping_type, int32_t phase_increment,
	                    int32_t time_stretch_ratio, int32_t source_amplitude, int32_t amplitude_increment,
	                    int32_t interpolation_buffer_size, int32_t priority_rating) = 0;
};

} // namespace deluge::synth
