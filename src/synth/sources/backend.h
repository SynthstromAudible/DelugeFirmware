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

struct BlockControls;
struct WavetableBlock;
struct Dx7Block;
struct SampleBlock;
struct InputBlock;
struct DualSourceControls;
struct FmControls;
struct UnisonPart;
struct SourceLevels;
struct VoiceConfig;
struct VoiceBlock;
struct NoteSetup;
struct SoundSnapshot;
struct ReleaseInfo;

/// @brief The numeric backend of the q31 Deluge instrument: what @ref BasicSource and @ref BasicEngine are built over.
/// A float instrument defines its own backend with the same member names (float samples and controls, its own
/// per-type blocks, voice and controls) and instantiates the same contracts; the Deluge instrument instantiates them
/// over this one only.
struct Fixed {
	using Sample = int32_t;  ///< q31
	using Control = int32_t; ///< q31 levels and amplitudes
	using Phase = uint32_t;  ///< UQ0.32

	// A source's per-block inputs beyond the common ones
	using OscillatorControls = synth::BlockControls;
	using Wavetable = synth::WavetableBlock;
	using Dx7 = synth::Dx7Block;
	using SampleInputs = synth::SampleBlock;
	using Input = synth::InputBlock;

	// The voice an engine plays and the controls it derives for each engine family
	using UnisonPart = synth::UnisonPart;
	using Levels = synth::SourceLevels;
	using Config = synth::VoiceConfig;
	using VoiceInputs = synth::VoiceBlock;
	using Setup = synth::NoteSetup;
	using Snapshot = synth::SoundSnapshot;
	using Release = synth::ReleaseInfo;
	using DualSourceControls = synth::DualSourceControls;
	using FmControls = synth::FmControls;
};

} // namespace deluge::synth
