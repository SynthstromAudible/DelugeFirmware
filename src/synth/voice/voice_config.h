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

#include "definitions_cxx.hpp"                  // SynthMode, kNumExpressionDimensions, kFirstLocalSource, VoicePriority
#include "dsp-next/fixed/filter/filter_modes.h" // LowPassSlot, HighPassSlot, FilterRoute
#include "synth/modulation/lfo.h"               // LFOConfig
#include "util/misc.h"                          // util::to_underlying
#include <array>
#include <bitset>
#include <cstdint>

namespace deluge::synth {

/// @brief A sound's render config for one block, the same for each of its voices: what the voice kernel's render reads
/// of the sound. The sound builds it once a block and hands it to each voice's render.
struct VoiceConfig {
	deluge::dsp::fixed::LowPassSlot low_pass;   ///< What the low-pass slot runs: OFF when the sound needs no low-pass
	deluge::dsp::fixed::HighPassSlot high_pass; ///< What the high-pass slot runs: OFF when the sound needs no high-pass
	deluge::dsp::fixed::FilterRoute filter_route; ///< How the two filters join
	int32_t volume_neutral_value_for_unison;      ///< Sound::volumeNeutralValueForUnison
	/// How far behind the engine's render is running, 0 when on time: the render takes cheaper paths as it grows.
	int32_t cpu_direness;
	VoicePriority voice_priority; ///< The sound's voice priority, which the cull order ranks by first

	SynthMode synth_mode;  ///< Sound::getSynthMode() — drives the subtractive/ring-mod/FM dispatch
	LFOConfig lfo2_config; ///< Sound::lfoConfig[LFO2_ID]
	LFOConfig lfo4_config; ///< Sound::lfoConfig[LFO4_ID]
	/// Sound::getSyncedLFOPhaseIncrement(lfoConfig[LFO2_ID/LFO4_ID]) — transport-synced increment, valid only when
	/// the LFO's syncLevel != NONE (0 otherwise, unused). Sound/transport-level, identical for all voices.
	uint32_t lfo2_synced_phase_increment;
	uint32_t lfo4_synced_phase_increment;

	bool has_filters; ///< Sound::hasFilters()

	std::bitset<kNumExpressionDimensions> expression_sources_changed_at_synth_level; ///< Sound:: (same name)
	std::array<int32_t, kNumExpressionDimensions> monophonic_expression_values;      ///< Sound:: (same name)

	uint8_t clipping_amount; ///< Sound::clippingAmount — gates + drives the output-stage saturation

	bool doing_osc_sync; ///< Sound::renderingOscillatorSyncCurrently(paramManager) (sound-level, same for all voices)
	bool
	    engine_rendering_in_stereo; ///< AudioEngine::renderInStereo (the engine-wide stereo-render flag; gates panning)

	uint8_t num_unison;                                  ///< Sound::numUnison
	uint8_t unison_stereo_spread;                        ///< Sound::unisonStereoSpread
	std::array<int32_t, kMaxNumVoicesUnison> unison_pan; ///< Sound::unisonPan (copied)

	std::array<uint32_t, kNumSources> osc_retrigger_phase; ///< Sound::oscRetriggerPhase (4294967295 == off)

	std::array<OscType, kNumSources> osc_type; ///< Sound::sources[s].oscType — drives the per-source render dispatch
	std::array<SampleRepeatMode, kNumSources> repeat_mode; ///< Sound::sources[s].repeatMode (play-once auto-release)
	std::array<InterpolationMode, kNumSources>
	    interpolation_mode;                         ///< Sound::sources[s].sampleControls.interpolationMode
	std::array<bool, kNumSources> dx_patch_changed; ///< Sound::sources[s].dxPatchChanged (DX7 patch re-derive signal)

	/// Sound::params.sourceValues — the *global* (non-local) patch-source values, copied into the voice's local
	/// data plane before per-block patching. Settled before the voice loop (sound-level patching precedes it),
	/// so a once-per-block snapshot is bit-exact.
	std::array<int32_t, util::to_underlying(kFirstLocalSource)> global_source_values;

	/// Sound::allowNoteTails(modelStack, /*disregardSampleLoop=*/true) — the note-tail re-check for a voice that
	/// previously ignored a note-off (e.g. preset changed mid-note). Sound/clip-level, identical for all voices.
	bool note_tails_allowed;

	/// ModelStackWithSoundFlags::checkSourceEverActive(s) — the per-source "ever active" soundFlags. Sound/
	/// paramManager-derived, computed once on the shared per-render modelStack, so identical for all voices.
	std::array<bool, kNumSources> source_ever_active;

	/// paramManager->getPatchedParamSet()->params[LOCAL_NOISE_VOLUME].containsSomething(INT32_MIN) — whether a
	/// noise source is in play. The predicate areAllUnisonPartsInactive gates on (sound-level, loop-stable).
	bool noise_volume_contains_something;

	/// paramManager->getPatchCableSet()->sourcesPatchedToAnything[GLOBALITY_LOCAL] — bitmask of which local patch
	/// sources are wired (the local envelope/LFO render gates on it). Set by setupPatching, loop-stable.
	uint32_t local_sources_patched_to_anything;

	/// The resolved MPE bend ranges ([BEND_RANGE_MAIN], [BEND_RANGE_FINGER_LEVEL]) — the sound's
	/// ExpressionParamSet::bendRanges, or FlashStorage::defaultBendRange when there's no expression set.
	std::array<uint8_t, 2> bend_ranges;
	/// paramManager->getUnpatchedParamSet()->getValue(UNPATCHED_PORTAMENTO) — raw porta param (the
	/// cableToExpParamShortcut/lookupReleaseRate transform stays in the render).
	int32_t portamento;

	/// Sound::modulator1ToModulator0 — FM modulator-routing flag (whether mod1 modulates mod0).
	bool modulator1_to_modulator0;
	/// Sound::isSourceActiveCurrently(s, paramManager) per source — sound-level (paramManager + source config),
	/// loop-stable; the subtractive engine's per-source render gate.
	std::array<bool, kNumSources> source_active_currently;
};

} // namespace deluge::synth
