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

#include "instruments/classic/settings.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "serialization/value.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/// @file
/// @brief Building a sound's record in a spec or fixture through the instrument's own surface: a settings row by its
/// file path and text (what a menu or a file writes), and the rule for a switch of oscillator kind. With the
/// state_access writers for ranges and a DX7 patch, and switch_engine for an engine a menu would refuse, that builds
/// every sound a file can hold. A spec reads the record back through state_access.
namespace deluge::test {

/// @brief @p state with settings row @p path written as the file text @p text, as a menu's write makes it, or nothing
/// where the instrument refuses the row or the value.
/// @param state The record.
/// @param path A `kSettings` row's path, e.g. "osc1/transpose".
/// @param text The value as a file writes it, e.g. "5" or "sample".
/// @return The record written, or nothing.
[[nodiscard]] inline std::optional<instruments::classic::SoundState>
try_set(instruments::classic::SoundState state, std::string_view path, std::string_view text) {
	const std::optional<serialization::Value> value =
	    instruments::classic::canonical_setting(path, serialization::Value::makeScalar(std::string(text)));
	if (!value || !instruments::classic::allows_setting(state, path, *value)) {
		return std::nullopt;
	}
	(void)instruments::classic::write_setting(state, path, *value);
	return state;
}

/// @brief @p state with settings row @p path written as the file text @p text (try_set()). A row or value the
/// instrument refuses stops the run: a fixture that builds a sound must build the sound it names.
/// @return @p state, written.
inline instruments::classic::SoundState& set(instruments::classic::SoundState& state, std::string_view path,
                                             std::string_view text) {
	std::optional<instruments::classic::SoundState> written = try_set(state, path, text);
	if (!written) {
		std::fprintf(stderr, "record_state: %.*s refuses %.*s\n", static_cast<int>(path.size()), path.data(),
		             static_cast<int>(text.size()), text.data());
		std::abort();
	}
	state = std::move(*written);
	return state;
}

/// @brief A copy of @p state with settings row @p path written as @p text (set()).
[[nodiscard]] inline instruments::classic::SoundState with(instruments::classic::SoundState state,
                                                           std::string_view path, std::string_view text) {
	set(state, path, text);
	return state;
}

/// @brief A copy of @p state with slot @p id switched to kind @p type, as the menu's type row switches it.
[[nodiscard]] inline instruments::classic::SoundState with_kind(const instruments::classic::SoundState& state,
                                                                instruments::classic::SourceId id,
                                                                instruments::classic::OscType type) {
	return instruments::classic::switch_source(state, id, type, instruments::classic::source_ranges(state, id),
	                                           instruments::classic::source_type(state, id)
	                                               == instruments::classic::OscType::WAVETABLE);
}

} // namespace deluge::test

#pragma pop_macro("context")
