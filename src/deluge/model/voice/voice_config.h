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

#include "definitions_cxx.hpp"                            // SynthMode, kNumExpressionDimensions, kFirstLocalSource
#include "model/mod_controllable/filters/filter_config.h" // FilterMode, FilterRoute
#include "synth/lfo.h"                                    // LFOConfig
#include "util/misc.h"                                    // util::to_underlying
#include <array>
#include <bitset>
#include <cstdint>

/// @brief A per-sound snapshot of the render config the voice kernel consumes — built by the adapter
/// (`Sound::render`) and handed to `Voice::render`. The first step of severing `Voice::render`'s `Sound&`
/// coupling (Phase 4, see `docs/dev/synth_phase4_voice_kernel.md`): config the render used to read inline off
/// `sound` now arrives through this struct, populated once at the adapter edge.
///
/// Grows cluster-by-cluster across Phase-4 slices (this slice: the filter-set config). App-side for now — its
/// filter-enum fields live under `model/`, which `synth/` may not include; it relocates to `synth/` once the
/// boundary types are numeric-agnostic.
struct VoiceConfig {
	FilterMode lpfMode;                  ///< Sound::lpfMode (the render still gates this with its doLPF arg)
	FilterMode hpfMode;                  ///< Sound::hpfMode (gated with doHPF)
	FilterRoute filterRoute;             ///< Sound::filterRoute
	int32_t volumeNeutralValueForUnison; ///< Sound::volumeNeutralValueForUnison

	SynthMode synthMode;  ///< Sound::getSynthMode() — drives the subtractive/ring-mod/FM dispatch
	LFOConfig lfo2Config; ///< Sound::lfoConfig[LFO2_ID]
	LFOConfig lfo4Config; ///< Sound::lfoConfig[LFO4_ID]

	bool hasFilters; ///< Sound::hasFilters()

	std::bitset<kNumExpressionDimensions> expressionSourcesChangedAtSynthLevel; ///< Sound:: (same name)
	std::array<int32_t, kNumExpressionDimensions> monophonicExpressionValues;   ///< Sound:: (same name)

	uint8_t clippingAmount; ///< Sound::clippingAmount — gates + drives the output-stage saturation

	bool doingOscSync; ///< Sound::renderingOscillatorSyncCurrently(paramManager) (sound-level, same for all voices)

	uint8_t numUnison;                                  ///< Sound::numUnison
	uint8_t unisonStereoSpread;                         ///< Sound::unisonStereoSpread
	std::array<int32_t, kMaxNumVoicesUnison> unisonPan; ///< Sound::unisonPan (copied)

	std::array<uint32_t, kNumSources> oscRetriggerPhase; ///< Sound::oscRetriggerPhase (4294967295 == off)

	std::array<OscType, kNumSources> oscType; ///< Sound::sources[s].oscType — drives the per-source render dispatch
	std::array<SampleRepeatMode, kNumSources> repeatMode; ///< Sound::sources[s].repeatMode (play-once auto-release)
	std::array<InterpolationMode, kNumSources>
	    interpolationMode;                        ///< Sound::sources[s].sampleControls.interpolationMode
	std::array<bool, kNumSources> dxPatchChanged; ///< Sound::sources[s].dxPatchChanged (DX7 patch re-derive signal)

	/// Sound::params.sourceValues — the *global* (non-local) patch-source values, copied into the voice's local
	/// data plane before per-block patching. Settled before the voice loop (sound-level patching precedes it),
	/// so a once-per-block snapshot is bit-exact.
	std::array<int32_t, util::to_underlying(kFirstLocalSource)> globalSourceValues;

	/// Sound::allowNoteTails(modelStack, /*disregardSampleLoop=*/true) — the note-tail re-check for a voice that
	/// previously ignored a note-off (e.g. preset changed mid-note). Sound/clip-level, identical for all voices.
	bool noteTailsAllowed;

	/// ModelStackWithSoundFlags::checkSourceEverActive(s) — the per-source "ever active" soundFlags. Sound/
	/// paramManager-derived, computed once on the shared per-render modelStack, so identical for all voices.
	std::array<bool, kNumSources> sourceEverActive;

	/// paramManager->getPatchedParamSet()->params[LOCAL_NOISE_VOLUME].containsSomething(INT32_MIN) — whether a
	/// noise source is in play. The predicate areAllUnisonPartsInactive gates on (sound-level, loop-stable).
	bool noiseVolumeContainsSomething;

	/// paramManager->getPatchCableSet()->sourcesPatchedToAnything[GLOBALITY_LOCAL] — bitmask of which local patch
	/// sources are wired (the local envelope/LFO render gates on it). Set by setupPatching, loop-stable.
	uint32_t localSourcesPatchedToAnything;
};
