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

#include "instruments/deluge/sound_state.h"
#include "support/printers/effect_states_printer.h"
#include "support/printers/list_printer.h"
#include "support/printers/optional_printer.h"

#include <array>
#include <cstddef>
#include <format>
#include <ostream>
#include <span>
#include <utility>

/// @file
/// @brief Prints a sound's state, with its sources' key-split ranges and DX7 patches, in a failed CppSpec comparison,
///        alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::instrument {

/// @brief Prints a range field by field, e.g.
///        `{top_note 60, file "SAMPLES/A.WAV", start 0, end 9999999, loop_start 0, loop_end 0, transpose 0, cents 0}`.
/// @param os The stream to print to.
/// @param r The range.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const RangeState& r) {
	return os << std::format("{{top_note {}, file \"{}\", start {}, end {}, loop_start {}, loop_end {}, transpose {}, "
	                         "cents {}}}",
	                         r.top_note, r.file_path, r.start_pos, r.end_pos, r.loop_start_pos, r.loop_end_pos,
	                         r.transpose, r.cents);
}

/// @brief Prints a DX7 patch as its 156 voice bytes, its engine mode and its random detune.
/// @param os The stream to print to.
/// @param p The patch.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DxPatchState& p) {
	os << "{params [";
	for (std::size_t i = 0; i < p.params.size(); ++i) {
		os << (i == 0 ? "" : " ") << int{p.params[i]};
	}
	return os << std::format("], engine_mode {}, random_detune {}}}", p.engine_mode, p.random_detune);
}

/// @brief Prints an array of enums as their numbers in brackets, e.g. `[0 0 2 1]`.
/// @param os The stream to print to.
/// @param values The enums.
/// @return @p os.
template <typename E, std::size_t N>
std::ostream& print_enums(std::ostream& os, const std::array<E, N>& values) {
	os << '[';
	for (std::size_t i = 0; i < N; ++i) {
		os << (i == 0 ? "" : " ") << int{std::to_underlying(values[i])};
	}
	return os << ']';
}

/// @brief Prints a source field by field, an enum as its number, an absent DX7 patch as `none`, e.g.
///        `{osc_type 0, transpose 0, cents 0, repeat_mode 0, time_stretch_amount 0, interpolation 0,
///        pitch_and_speed_independent 0, reversed 0, dx_patch none, ranges [{top_note 60, …}]}`.
/// @param os The stream to print to.
/// @param s The source.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SourceState& s) {
	os << std::format(
	    "{{osc_type {}, transpose {}, cents {}, repeat_mode {}, time_stretch_amount {}, interpolation {}, "
	    "pitch_and_speed_independent {}, reversed {}, dx_patch ",
	    std::to_underlying(s.osc_type), s.transpose, s.cents, std::to_underlying(s.repeat_mode), s.time_stretch_amount,
	    std::to_underlying(s.interpolation_mode), int{s.pitch_and_speed_independent}, int{s.reversed});
	deluge::test::printers::print_optional(os, s.dx_patch) << ", ranges ";
	return deluge::test::printers::print_list(os, std::span{s.ranges}) << "}";
}

/// @brief Prints a sound's state field by field, an enum as its number, its effects as their encoding and its two
///        sources, e.g. `{voice_priority 1, max_voice_count 0, …, effects { … }, sources [{osc_type 0, …} {…}]}`.
/// @param os The stream to print to.
/// @param s The state.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SoundState& s) {
	using deluge::test::printers::print_list;
	os << std::format("{{voice_priority {}, max_voice_count {}, side_chain_send_level {}, output_midi_channel {}, "
	                  "output_midi_note_for_drum {}, oscillator_sync {}, polyphonic {}, synth_mode {}, transpose {}, "
	                  "cents {}, num_unison {}, unison_detune {}, unison_stereo_spread {}, "
	                  "modulator1_to_modulator0 {}, modulator_transpose ",
	                  std::to_underlying(s.voice_priority), s.max_voice_count, s.side_chain_send_level,
	                  s.output_midi_channel, s.output_midi_note_for_drum, int{s.oscillator_sync},
	                  std::to_underlying(s.polyphonic), std::to_underlying(s.synth_mode), s.transpose, s.cents,
	                  s.num_unison, s.unison_detune, s.unison_stereo_spread, int{s.modulator1_to_modulator0});
	print_list(os, std::span{s.modulator_transpose}) << ", modulator_cents [";
	os << int{s.modulator_cents[0]} << ' ' << int{s.modulator_cents[1]} << "], osc_retrigger_phase ";
	print_list(os, std::span{s.osc_retrigger_phase}) << ", modulator_retrigger_phase ";
	print_list(os, std::span{s.modulator_retrigger_phase}) << ", lfo_wave_type ";
	print_enums(os, s.lfo_wave_type) << ", lfo_sync_type ";
	print_enums(os, s.lfo_sync_type) << ", lfo_sync_level ";
	print_enums(os, s.lfo_sync_level) << ", effects " << s.effects << ", sources ";
	return print_list(os, std::span{s.sources}) << "}";
}

} // namespace deluge::instrument
