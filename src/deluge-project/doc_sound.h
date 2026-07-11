/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/doc_mod_controllable.h"
#include "deluge-project/doc_source.h"

#include <cstdint>

namespace deluge::project {

/// The DOM-owned structural record for one engine Sound (M3 owner-5). NESTED as std::optional<DocSound> inside the
/// owner's DocOutput (SoundInstrument) or DocDrum (SoundDrum) — a Sound is owned 1:1. Params are already in the DOM
/// param tier; this holds Sound's NON-param structure. Slice B = cold scalars; later slices grow it (hot scalars,
/// LFO, modKnobs, sources). Enums stored as underlying values.
struct DocSound {
	uint8_t voice_priority{0};
	uint8_t max_voice_count{0};
	int32_t side_chain_send_level{0};
	uint8_t output_midi_channel{0};
	uint8_t output_midi_note_for_drum{0};
	bool oscillator_sync{false};

	// Slice C: remaining top-level Sound scalars (hot + cold; shadow only, no read-flip). Arrays are size 2
	// (kNumSources == kNumModulators == 2).
	uint8_t polyphonic{0}; ///< PolyphonyMode underlying
	uint8_t synth_mode{0}; ///< SynthMode underlying
	int16_t transpose{0};
	uint8_t num_unison{0};
	int8_t unison_detune{0};
	uint8_t unison_stereo_spread{0};
	bool modulator1_to_modulator0{false};
	int16_t modulator_transpose[2]{};
	int8_t modulator_cents[2]{};
	uint32_t osc_retrigger_phase[2]{};
	uint32_t modulator_retrigger_phase[2]{};

	// Slice D: LFO config — 4 LFOs (LFO_COUNT == 4), each {waveType, syncType, syncLevel} stored as underlying.
	uint8_t lfo_wave_type[4]{};
	uint8_t lfo_sync_type[4]{};
	uint8_t lfo_sync_level[4]{};

	// modKnobs: physical mod-knob assignments [kNumModButtons=8][kNumPhysicalModKnobs=2]. Each knob's entire
	// persistent state is its ParamDescriptor (a single uint32_t); everything the serializer emits (the controlled
	// param + patch sources) derives from it.
	uint32_t mod_knob_param_descriptor[8][2]{};

	// Slice F: shared non-param ModControllableAudio structure (filter/modFX + delay/sidechain/compressor/stutter).
	DocModControllable mod_controllable;

	// Slice G: the two oscillator Sources (kNumSources == 2).
	DocSource sources[2];
};

} // namespace deluge::project
