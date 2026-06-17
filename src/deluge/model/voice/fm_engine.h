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

#include <cstdint>

class Voice;
class Sound;
struct VoiceConfig;

/// @brief The FM synth engine: the cross-source driver for `SynthMode::FM` (per the L1 reframe,
/// `docs/dev/synth_l1_engine_architecture.md`). It owns one unison part's FM topology — modulator pitch
/// derivation, the modulators (with the `modulator1ToModulator0` routing and the carriers-only fallback when no
/// modulators are active), the carriers (FM'd by the modulators, with feedback), and the stereo-unison fold.
///
/// **Transitional shape** (like @ref SubtractiveEngine): the per-operator sine/FM-with-feedback renders and the
/// pitch math still live in Voice (`renderSineWaveWithFeedback` / `renderFMWithFeedback` /
/// `renderFMWithFeedbackAdd` / `adjustPitch`, all private), so the engine **delegates** to them via the passed-in
/// `Voice&` and is a `friend` of Voice. Stateless for now; the modulator/carrier phase + feedback state stays on
/// the VoiceUnisonParts.
class FMEngine {
public:
	/// Render one unison part of FM, accumulating into @p oscBuffer. The per-source/per-modulator arrays +
	/// per-unison amplitudes are computed by Voice and passed in. @p modulatorsActive is **mutable** — entries
	/// are knocked to false on pitch-too-high and (faithful to the inline code) that persists across unison
	/// parts. @p modulatorScratch / @p carrierScratch are the caller's spare buffers (spareRenderingBuffer[2]/[3]).
	void render(Voice& voice, int32_t u, int32_t numSamples, int32_t overallPitchAdjust, bool stereoUnison,
	            int32_t unisonAmplitudeL, int32_t unisonAmplitudeR, const uint32_t* phaseIncrements,
	            const int32_t* sourceAmplitudes, const int32_t* sourceAmplitudesNow,
	            const int32_t* sourceAmplitudeIncrements, bool* modulatorsActive,
	            const int32_t* modulatorAmplitudeIncrements, int32_t* oscBuffer, int32_t* modulatorScratch,
	            int32_t* carrierScratch, const VoiceConfig& cfg);
};
