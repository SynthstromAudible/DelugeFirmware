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

#include "synth/sources/sample/sample_source.h"
#include "definitions_cxx.hpp"
#include "dsp-next/core/q31_ops.h"
#include "synth/sources/sample/time_stretch_adjust.h"
#include <cstdint>
#include <cstdlib> // std::abs
#include <cstring> // memset
#include <utility> // std::cmp_not_equal

namespace deluge::synth {

// Late start, sync, caching and stereo unison, each a branch:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
SourceRenderResult SampleSource::render(const SourceBlock& block) {
	int32_t* __restrict__ osc_buffer = block.osc_buffer;
	const int32_t num_samples = block.num_samples;
	const int32_t source_amplitude = block.source_amplitude;
	const int32_t amplitude_increment = block.amplitude_increment;
	uint32_t phase_increment = block.phase_increment; // mutated below by getPitchAndSpeedParams (taken by address)
	const uint32_t phase_increment_stored_value = block.sample.phase_increment_stored_value;
	const bool stereo_unison = block.stereo_unison;
	const int32_t amplitude_l = block.amplitude_l;
	const int32_t amplitude_r = block.amplitude_r;
	int32_t* stereo_unison_scratch = block.stereo_scratch;

	const int32_t num_channels = port_->num_channels();

	// (The legacy TEST_SAMPLE_LOOP_POINTS fuzzing block was removed here: it mutated voice/sound state via
	// Voice&/paramManager, incompatible with the synth::Source contract. A debug-only #ifdef, never in normal or
	// golden builds; reintroduce app-side if wanted.)

	// First figure out the time-stretching amount.
	uint32_t time_stretch_ratio = 0;
	uint32_t note_length_in_samples = 0;

	bool still_ok = get_pitch_and_speed_params(&phase_increment, &time_stretch_ratio, &note_length_in_samples);
	if (!still_ok) {
		return SourceRenderResult::UNASSIGN;
	}

	// If user unmuted mid-note...
	bool try_to_start_mid_note = port_->pending_samples_late() != 0u;
	if (try_to_start_mid_note) [[unlikely]] {

		int32_t raw_samples_late = 0;

		// Synced / STRETCH - it's super easy.
		if (port_->is_stretch_repeat_mode()) {
			raw_samples_late = static_cast<int32_t>(port_->synced_num_samples_in());
		}

		// Or, normal - it needs a bit more explanation.
		else {
			// We have to ignore any pitch modulation, aka the "patched" value for phaseIncrement: we have to
			// use phaseIncrementStoredValue instead, which is the pre-modulation/patching value. But we also
			// have to forget about the timeStretchRatio we calculated above with the call to
			// getPitchAndSpeedParams(), and instead calculate a special version of this with this call to
			// getSpeedParamForNoSyncing(), which is what getPitchAndSpeedParams() itself calls when not in
			// STRETCH mode, which we've already determined we're not in, and crucially pass it the not-patched
			// phaseIncrementStoredValue. This fix was done in September 2020 after bug
			// report from Clyde.
			uint32_t time_stretch_ratio_without_modulation = get_speed_param_for_no_syncing(
			    static_cast<int32_t>(phase_increment_stored_value), port_->neutral_phase_increment());

			// Cool, so now we've got phaseIncrement and timeStretchRatio equivalent values which will indicate
			// our correct play position into the sample regardless of pitch modulation (almost always vibrato).
			raw_samples_late = static_cast<int32_t>(
			    (((static_cast<uint64_t>(port_->pending_samples_late()) * phase_increment_stored_value) >> 24)
			     * time_stretch_ratio_without_modulation)
			    >> 24);
		}

		switch (port_->attempt_late_start(raw_samples_late, num_samples)) {
		case SamplePlaybackPort::LateStartOutcome::FAILURE:
			return SourceRenderResult::UNASSIGN;
		case SamplePlaybackPort::LateStartOutcome::WAIT:
			return SourceRenderResult::SKIP;
		case SamplePlaybackPort::LateStartOutcome::OK:
			break; // it started fine!
		}
	}

	LoopType looping_type = port_->looping_type();

	// Read by the reader only when the pitch changes; without a pitch change no interpolation runs.
	int32_t interpolation_buffer_size = kInterpolationMaxNumSamples;

	// If pitch adjustment...
	if (phase_increment != kMaxSampleValue) {

		// Work out what quality we're going to do that at
		interpolation_buffer_size = port_->interpolation_buffer_size(phase_increment);

		// And if first render, and other conditions met, see if we can use cache.
		// It may seem like it'd be a good idea to try and set this up on note-on, rather than here in the
		// rendering routine, but I tried that and the fact is that it means a bunch of extra computation has to
		// happen to work out pitch and timestretch there as well as here (where it'll be worked out anyway),
		// including checking the result of patching / modulation (and we *do* allow caching where, say,
		// velocity or note is affecting pitch), and stretch-syncing.
		if (!port_->done_first_render_yet() && !try_to_start_mid_note && block.sample.porta_envelope_pos == kNoPorta) {

			// If looping, make sure the loop isn't too short. If so, caching just wouldn't sound good /
			// accurate
			if (looping_type != LoopType::NONE) {
				SamplePlaybackPort::LoopPoints loop = port_->loop_points();

				int32_t loop_length = loop.end - loop.start;
				loop_length = std::abs(loop_length);
				uint64_t phase_increment_times_time_stretch_ratio =
				    (static_cast<uint64_t>(phase_increment) * time_stretch_ratio) >> 24;
				auto loop_length_cached =
				    static_cast<int32_t>((static_cast<uint64_t>(static_cast<uint32_t>(loop_length)) << 24)
				                         / phase_increment_times_time_stretch_ratio);
				if (loop_length_cached < 2205) {
					goto dontUseCache; // Limit is 50mS i.e. 20hZ
				}
			}

			// If a changeable source is patched to pitch, don't cache. This scan was hoisted app-side into
			// block.sample.pitch_mod_disallows_cache (it reaches the patch cables + the aftertouch/sidechain source
			// values), so render needn't reach paramManager / the param planes.
			if (block.sample.pitch_mod_disallows_cache) {
				goto dontUseCache;
			}

			{
				// If still here, we can use cache
				bool everything_ok = port_->possibly_set_up_cache(static_cast<int32_t>(phase_increment),
				                                                  static_cast<int32_t>(time_stretch_ratio),
				                                                  block.sample.priority_rating, looping_type);
				if (!everything_ok) {
					return SourceRenderResult::UNASSIGN;
				}
			}

dontUseCache: {}
		}
	}

	int32_t* render_buffer = osc_buffer;

	if (stereo_unison) [[unlikely]] {
		// TODO: I first wanted to integrate this with voiceSample->render()'s own
		// amplitude control but it is just too complex - multiple copies of
		// the amp logic depending if caching is used or not, timestretching or not,
		// for now settle for "don't pay what you don't use", i e no extra copies/maths
		// if you don't use unison stereo on a sample - bfredl
		render_buffer = stereo_unison_scratch; // note: spareRenderingBuffer[2] and [3] are used
		memset(render_buffer, 0, 2 * SSI_TX_BUFFER_NUM_SAMPLES * sizeof(int32_t));
	}

	// We no longer do caching when there's just time stretching with no pitch adjustment, because the time
	// stretching algorithm is so efficient, playing back the cache is hardly any faster than just doing the
	// time stretching (once perc info has been cached) - and, crucially, creating / writing to the cache in the
	// first place is quite inefficient when time stretching, because when we're not writing to the cache, that
	// allows us to do a special optimization not otherwise available (that is, combining the amplitude
	// increments for the hop crossfades with the overall voice ones, and having multiple crossfading hops write
	// directly to the osc buffer).

	bool still_active = port_->render(render_buffer, num_samples, looping_type, static_cast<int32_t>(phase_increment),
	                                  static_cast<int32_t>(time_stretch_ratio), source_amplitude, amplitude_increment,
	                                  interpolation_buffer_size, block.sample.priority_rating);

	if (stereo_unison) {
		if (num_channels == 2) {
			// TODO: society if renderBasicSource() took a StereoSample[] buffer already
			for (int32_t i = 0; i < num_samples; i++) {
				osc_buffer[(i << 1)] += multiply_32x32_rshift32(render_buffer[(i << 1)], amplitude_l) << 2;
				osc_buffer[(i << 1) + 1] += multiply_32x32_rshift32(render_buffer[(i << 1) + 1], amplitude_r) << 2;
			}
		}
		else {
			// TODO: if render buffer was typed we could use addPannedMono()
			for (int32_t i = 0; i < num_samples; i++) {
				osc_buffer[(i << 1)] += multiply_32x32_rshift32(render_buffer[i], amplitude_l) << 2;
				osc_buffer[(i << 1) + 1] += multiply_32x32_rshift32(render_buffer[i], amplitude_r) << 2;
			}
		}
	}

	if (!still_active) {
		return SourceRenderResult::UNASSIGN;
	}
	return SourceRenderResult::ACTIVE;
}

// Sync to the sequence and the pitch/speed modes, each a branch:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool SampleSource::get_pitch_and_speed_params(uint32_t* phase_increment, uint32_t* time_stretch_ratio,
                                              uint32_t* note_length_in_samples) {

	int32_t pitch_adjust_neutral_value = port_->neutral_phase_increment();

	// If syncing...
	if (port_->sequence_sync_length_ticks() != 0u) {

		*time_stretch_ratio = kMaxSampleValue;

		// That is, after converted to 44.1kHz
		uint32_t sample_length_in_samples = port_->sample_length_at_system_sample_rate();
		*note_length_in_samples = (port_->time_per_internal_tick_big() * port_->sequence_sync_length_ticks())
		                          >> 32; // No rounding. Should be fine?

		// To stop things getting insane, limit to 32x speed
		if ((sample_length_in_samples >> 5) > *note_length_in_samples) {
			return false;
		}

		// If time stretch, achieve syncing that way
		if (port_->pitch_and_speed_independent()) {
			*time_stretch_ratio = static_cast<uint64_t>(kMaxSampleValue)
			                      * static_cast<uint64_t>(sample_length_in_samples) / *note_length_in_samples;

			// And if pitch was manually adjusted too, that's fine - counteract that by adjusting the time-stretch
			// amount more
			if (std::cmp_not_equal(*phase_increment, pitch_adjust_neutral_value)) {
				*time_stretch_ratio =
				    (static_cast<uint64_t>(*time_stretch_ratio) * pitch_adjust_neutral_value) / *phase_increment;
			}

			// Or if no manual pitch adjustment...
			else {

				// If we'd only be time-stretching a tiiiny bit (1/10th of an octave either direction)...
				if (*time_stretch_ratio >= 15653696 && *time_stretch_ratio < 17981375) {

					// And if we're less than 7.8mS out of sync...
					int32_t num_samples_lagging_behind_sync = port_->num_samples_lagging_behind_sync();
					int32_t num_samples_drift = std::abs(num_samples_lagging_behind_sync);

					if (num_samples_drift < (port_->sample_rate() >> 7)) {

						// We can just not time-stretch... for now
						*time_stretch_ratio = kMaxSampleValue;
					}
				}
			}
		}

		// Or if pitch-stretch, achieve syncing that way
		else {

			// But first, if pitch was manually adjusted as well, counteract that by adjusting the time-stretch amount
			// more
			bool further_pitch_shifting = std::cmp_not_equal(*phase_increment, pitch_adjust_neutral_value);
			if (further_pitch_shifting) {
				*time_stretch_ratio = (static_cast<uint64_t>(pitch_adjust_neutral_value) << 24) / *phase_increment;
			}
			*phase_increment = static_cast<uint64_t>(*phase_increment) * static_cast<uint64_t>(sample_length_in_samples)
			                   / *note_length_in_samples;

			// If we're not time stretching / pitch shifting
			if (!further_pitch_shifting) {
				*phase_increment = port_->adjust_pitch_to_correct_drift_from_sync(*phase_increment);
			}
		}
	}

	// Or if no syncing...
	else {

		*time_stretch_ratio =
		    get_speed_param_for_no_syncing(static_cast<int32_t>(*phase_increment), pitch_adjust_neutral_value);
	}

	return true;
}

// This normally only gets called from the getPitchAndSpeedParams() function, above, but occasionally we'll also call it
// from render() when doing a "late start" on a Sample and we need to disregard any pitch modulation, so send this the
// non-modulated phaseIncrement.
uint32_t SampleSource::get_speed_param_for_no_syncing(int32_t phase_increment, int32_t pitch_adjust_neutral_value) {

	uint32_t time_stretch_ratio = kMaxSampleValue;

	// If pitch and time being treated independently, then achieve this by adjusting stretch to counteract pitch
	if (port_->pitch_and_speed_independent() && phase_increment != pitch_adjust_neutral_value) {
		time_stretch_ratio =
		    (static_cast<uint64_t>(pitch_adjust_neutral_value) << 24) / static_cast<uint32_t>(phase_increment);
	}

	// And whether or not that was the case, if there's a manual adjustment to time-stretch, apply that now
	if (port_->time_stretch_amount() != 0) {
		time_stretch_ratio = (static_cast<uint64_t>(time_stretch_ratio)
		                      * deluge::synth::kTimeStretchAdjust[port_->time_stretch_amount() + 48])
		                     >> 24;
	}

	return time_stretch_ratio;
}

} // namespace deluge::synth
