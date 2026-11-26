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

#include "instruments/classic/sound_state.h"
#include "support/printers/effect_states_printer.h"
#include "support/printers/fixed_point_printer.h"
#include "support/printers/list_printer.h"
#include "support/printers/optional_printer.h"
#include "synth/engines/engine_table.h"
#include "synth/modulation/lfo_state.h"
#include "synth/sources/source_state.h"
#include "synth/voice/voicing_state.h"

#include <cstddef>
#include <format>
#include <ostream>
#include <span>
#include <utility>
#include <variant>

/// @file
/// @brief Printers for a sound's record and its components' states, every field, an enum as its number.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operators live in the values' own namespace so that lookup finds them from inside CppSpec.
/// Every spec that compares these types includes this header: whether a type streams is decided once per type in a
/// binary, so a spec linked with others that print the same type must agree with them.
namespace deluge::synth {

/// @brief Prints a DX7 patch as its 156 voice bytes, its engine mode and its random detune.
inline std::ostream& operator<<(std::ostream& os, const DxPatch& p) {
	os << "{params [";
	for (std::size_t i = 0; i < p.params.size(); ++i) {
		os << (i == 0 ? "" : " ") << int{p.params[i]};
	}
	return os << std::format("], engine_mode {}, random_detune {}}}", p.engine_mode, p.random_detune);
}

/// @brief Prints a tuning, e.g. `{transpose -12, cents 5}`.
inline std::ostream& operator<<(std::ostream& os, const Tuning& t) {
	return os << std::format("{{transpose {}, cents {}}}", t.transpose, t.cents);
}

/// @brief Prints a pitch, its retrigger phase as the fixed-point word or `none` for free-running.
inline std::ostream& operator<<(std::ostream& os, const Pitch& p) {
	os << "{tuning " << p.tuning << ", retrigger ";
	return deluge::test::printers::print_optional(os, p.retrigger) << "}";
}

/// @brief Prints a VA oscillator's state.
inline std::ostream& operator<<(std::ostream& os, const VaState& s) {
	return os << "{va, pitch " << s.pitch << ", shape " << std::to_underlying(s.shape) << "}";
}

/// @brief Prints a source range, its zone in samples.
inline std::ostream& operator<<(std::ostream& os, const SourceRange& r) {
	return os << std::format("{{top_note {}, file \"{}\", start {}, end {}, loop_start {}, loop_end {}, tuning ",
	                         r.top_note, r.file_path, r.zone.start, r.zone.end, r.zone.loop_start, r.zone.loop_end)
	          << r.tuning << "}";
}

/// @brief Prints a wavetable oscillator's state.
inline std::ostream& operator<<(std::ostream& os, const WavetableState& s) {
	os << "{wavetable, pitch " << s.pitch << ", ranges [";
	// A wavetable's range holds only its note and its file (the record clears the rest), so only those print
	for (size_t i = 0; i < s.ranges.size(); i++) {
		os << (i == 0 ? "" : " ")
		   << std::format("{{top_note {}, file \"{}\"}}", s.ranges[i].top_note, s.ranges[i].file_path);
	}
	return os << "]}";
}

/// @brief Prints how a sample plays.
inline std::ostream& operator<<(std::ostream& os, const SamplePlayback& p) {
	return os << std::format("{{repeat_mode {}, time_stretch_amount {}, interpolation {}, pitch_and_speed_independent "
	                         "{}, reversed {}}}",
	                         std::to_underlying(p.repeat_mode), p.time_stretch_amount,
	                         std::to_underlying(p.interpolation), int{p.pitch_and_speed_independent}, int{p.reversed});
}

/// @brief Prints a sample oscillator's state.
inline std::ostream& operator<<(std::ostream& os, const SampleState& s) {
	os << "{sample, playback " << s.playback << ", ranges ";
	return deluge::test::printers::print_list(os, std::span{s.ranges}) << "}";
}

/// @brief Prints a DX7 oscillator's state.
inline std::ostream& operator<<(std::ostream& os, const Dx7State& s) {
	os << "{dx7, pitch " << s.pitch << ", patch ";
	return deluge::test::printers::print_optional(os, s.patch) << "}";
}

/// @brief Prints a live input's state.
inline std::ostream& operator<<(std::ostream& os, const InputState& s) {
	return os << "{input, pitch " << s.pitch << ", channel " << std::to_underlying(s.channel) << "}";
}

/// @brief Prints whichever kind a slot holds.
inline std::ostream& operator<<(std::ostream& os, const SlotState& s) {
	std::visit([&](const auto& kind) { os << kind; }, s);
	return os;
}

/// @brief Prints two oscillators and their sync.
inline std::ostream& operator<<(std::ostream& os, const DualSourceState& s) {
	return os << "{slots [" << s.slots[0] << " " << s.slots[1] << "], sync " << int{s.sync} << "}";
}

/// @brief Prints the subtractive engine's state.
inline std::ostream& operator<<(std::ostream& os, const SubtractiveState& s) {
	return os << "{subtractive " << s.oscillators << "}";
}

/// @brief Prints the ring modulator's state.
inline std::ostream& operator<<(std::ostream& os, const RingModState& s) {
	return os << "{ringmod " << s.oscillators << "}";
}

/// @brief Prints the FM engine's state.
inline std::ostream& operator<<(std::ostream& os, const FmState& s) {
	return os << "{fm, carriers [" << s.carriers[0] << " " << s.carriers[1] << "], sync " << int{s.sync}
	          << ", modulators [" << s.modulators[0] << " " << s.modulators[1] << "], modulator1_to_modulator0 "
	          << int{s.modulator1_to_modulator0} << "}";
}

/// @brief Prints whichever engine plays.
inline std::ostream& operator<<(std::ostream& os, const EngineState& s) {
	std::visit([&](const auto& engine) { os << engine; }, s);
	return os;
}

/// @brief Prints unison.
inline std::ostream& operator<<(std::ostream& os, const UnisonState& u) {
	return os << std::format("{{count {}, detune {}, spread {}}}", u.count, u.detune, u.spread);
}

/// @brief Prints how the sound voices its notes.
inline std::ostream& operator<<(std::ostream& os, const VoicingState& v) {
	return os << std::format("{{polyphony {}, max_voices {}, priority {}, master ", std::to_underlying(v.polyphony),
	                         v.max_voices, std::to_underlying(v.priority))
	          << v.master << ", unison " << v.unison << "}";
}

/// @brief Prints an LFO's state.
inline std::ostream& operator<<(std::ostream& os, const LfoState& l) {
	return os << std::format("{{shape {}, sync_type {}, sync_level {}}}", std::to_underlying(l.shape),
	                         std::to_underlying(l.sync_type), std::to_underlying(l.sync_level));
}

} // namespace deluge::synth

namespace deluge::instruments::classic {

/// @brief Prints what a sound plays, every component's state.
inline std::ostream& operator<<(std::ostream& os, const ComposedState& s) {
	return os << "{voicing " << s.voicing << ", engine " << s.engine << ", instrument_lfos [" << s.instrument_lfos[0]
	          << " " << s.instrument_lfos[1] << "], voice_lfos [" << s.voice_lfos[0] << " " << s.voice_lfos[1]
	          << "], fx " << s.fx << "}";
}

/// @brief Prints what a slot keeps for a switch back.
inline std::ostream& operator<<(std::ostream& os, const SlotMemory& m) {
	os << "{parked ";
	deluge::test::printers::print_optional(os, m.parked) << ", playback " << m.playback << ", ranges ";
	deluge::test::printers::print_list(os, std::span{m.ranges}) << ", patch ";
	return deluge::test::printers::print_optional(os, m.patch) << ", sample_pitch " << m.sample_pitch << "}";
}

/// @brief Prints what a sound keeps for a switch back.
inline std::ostream& operator<<(std::ostream& os, const Recall& r) {
	return os << "{slots [" << r.slots[0] << " " << r.slots[1] << "], modulators [" << r.modulators[0] << " "
	          << r.modulators[1] << "], modulator1_to_modulator0 " << int{r.modulator1_to_modulator0} << ", sync "
	          << int{r.sync} << "}";
}

/// @brief Prints a sound's record: what it plays and what it keeps.
inline std::ostream& operator<<(std::ostream& os, const SoundState& s) {
	return os << "{plays " << s.plays << ", recall " << s.recall << "}";
}

} // namespace deluge::instruments::classic
