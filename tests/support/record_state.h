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

// CppSpec spells `context` as a macro; a spec or scenario that includes this after cppspec.hpp must still see the
// instrument's headers as written.
#pragma push_macro("context")
#undef context

#include "instruments/deluge/settings.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"
#include "instruments/deluge/state_rules.h"
#include "serialization/value.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief Building a sound's record in a spec or fixture through the instrument's own surface: a settings row by its
/// file path and text (what a menu or a file writes), the rules for a switch of engine or oscillator kind, and the
/// state_access writers for ranges and a DX7 patch. A spec reads the record back through state_access.
namespace deluge::test {

/// @brief @p state with settings row @p path written as the file text @p text, as a menu's write makes it. A row or
/// value the instrument refuses stops the run: a fixture that builds a sound must build the sound it names.
/// @param state The record.
/// @param path A `kSettings` row's path, e.g. "osc1/transpose".
/// @param text The value as a file writes it, e.g. "5" or "sample".
/// @return @p state, written.
inline instrument::SoundState& set(instrument::SoundState& state, std::string_view path, std::string_view text) {
	const std::optional<serialization::Value> value =
	    instrument::canonical_setting(path, serialization::Value::makeScalar(std::string(text)));
	if (!value || !instrument::allows_setting(state, path, *value)) {
		std::fprintf(stderr, "record_state: %.*s refuses %.*s\n", static_cast<int>(path.size()), path.data(),
		             static_cast<int>(text.size()), text.data());
		std::abort();
	}
	(void)instrument::write_setting(state, path, *value);
	return state;
}

/// @brief A copy of @p state with settings row @p path written as @p text (set()).
[[nodiscard]] inline instrument::SoundState with(instrument::SoundState state, std::string_view path,
                                                 std::string_view text) {
	set(state, path, text);
	return state;
}

/// @brief A new synth playing @p mode with slots of kinds @p a and @p b, switched as the menus switch them (the rules:
/// what a switch away leaves is kept for a switch back).
[[nodiscard]] inline instrument::SoundState sound_with(instrument::SynthMode mode, instrument::OscType a,
                                                       instrument::OscType b) {
	using instrument::SourceId;
	instrument::SoundState state;
	state = instrument::switch_source(state, SourceId::A, a, {}, false);
	state = instrument::switch_source(state, SourceId::B, b, {}, false);
	return instrument::switch_engine(state, mode);
}

/// @brief A copy of @p state with slot @p id switched to kind @p type, as the menu's type row switches it.
[[nodiscard]] inline instrument::SoundState with_kind(const instrument::SoundState& state, instrument::SourceId id,
                                                      instrument::OscType type) {
	return instrument::switch_source(state, id, type, instrument::source_ranges(state, id),
	                                 instrument::source_type(state, id) == instrument::OscType::WAVETABLE);
}

} // namespace deluge::test

#pragma pop_macro("context")
