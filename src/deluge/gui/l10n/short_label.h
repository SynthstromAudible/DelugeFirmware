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

#include "gui/l10n/strings.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace deluge::l10n {

/// @brief The horizontal menus' short labels: a column label or short option that is not simply its string.
///
/// Display abbreviations for a narrow column, kept beside the setting names rather than in a language: the l10n
/// languages are for translations. A string without an entry is shown as itself.
inline constexpr auto kShortLabels = [] {
	using enum String;
	return std::to_array<std::pair<String, std::string_view>>({
	    {STRING_FOR_ALTERNATE, "ALT"},
	    {STRING_FOR_AS_PLAYED, "PLAY"},
	    {STRING_FOR_BASS_PROBABILITY, "BASS"},
	    {STRING_FOR_CHORD_POLYPHONY, "CHRD"},
	    {STRING_FOR_CHORD_PROBABILITY, "CPRO"},
	    {STRING_FOR_CHORD_SIMULATOR, "CSIM"},
	    {STRING_FOR_DOWN, "DOWN"},
	    {STRING_FOR_GLIDE_PROBABILITY, "GLID"},
	    {STRING_FOR_INCLUDE_IN_KIT_ARP, "KARP"},
	    {STRING_FOR_MODULATOR_1, "Mod1"},
	    {STRING_FOR_NOTE_MODE, "NMOD"},
	    {STRING_FOR_NOTE_PROBABILITY, "NOTE"},
	    {STRING_FOR_NUMBER_OF_OCTAVES, "OCTA"},
	    {STRING_FOR_NUMBER_OF_RATCHETS, "RATC"},
	    {STRING_FOR_OCTAVE_MODE, "OMOD"},
	    {STRING_FOR_PATTERN, "PATT"},
	    {STRING_FOR_RANDOM, "RAND"},
	    {STRING_FOR_RANDOMIZER_LOCK, "LOCK"},
	    {STRING_FOR_RATCHET_PROBABILITY, "RPRO"},
	    {STRING_FOR_REVERSE_PROBABILITY, "RVRS"},
	    {STRING_FOR_RHYTHM, "RHYT"},
	    {STRING_FOR_SEQUENCE_LENGTH, "LENG"},
	    {STRING_FOR_STEP_REPEAT, "REPE"},
	    {STRING_FOR_SWAP_PROBABILITY, "SWAP"},
	    {STRING_FOR_UP, "UP"},
	    {STRING_FOR_UP_DOWN, "UPDN"},
	    {STRING_FOR_WALK1, "WLK1"},
	    {STRING_FOR_WALK2, "WLK2"},
	    {STRING_FOR_WALK3, "WLK3"},
	});
}();

/// @brief The short form of `id` for a horizontal menu column, if it has one.
/// @param id The label's string.
/// @return Its short form, or nothing when the string itself is the label.
constexpr std::optional<std::string_view> shortForm(String id) {
	for (const auto& [key, label] : kShortLabels) {
		if (key == id) {
			return label;
		}
	}
	return std::nullopt;
}

/// @brief The label to show for `id` in a horizontal menu column: its short form, or the string itself.
/// @param id The label's string.
/// @return The text to draw.
std::string_view shortLabel(String id);

} // namespace deluge::l10n
