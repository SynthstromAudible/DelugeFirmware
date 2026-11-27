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

#include "definitions_cxx.hpp" // SynthMode
#include "synth/engines/dx7_engine.h"
#include "synth/engines/engine.h"
#include "synth/engines/engine_table.h"
#include "synth/engines/fm_engine.h"
#include "synth/engines/ring_mod_engine.h"
#include "synth/engines/subtractive_engine.h"
#include "synth/table_rows.h"
#include <tuple>

namespace deluge::synth {

namespace detail {

template <class Rows>
struct EnginesOf;

template <class... Rows>
struct EnginesOf<std::tuple<Rows...>> {
	using type = std::tuple<typename Rows::Engine...>;
};

} // namespace detail

/// @brief One of each engine in the engine table, and the factory that picks one by synth mode. Every engine keeps
/// its state while another plays, so a voice whose sound changes mode while it sounds (an undo, a preset applied)
/// carries each engine's state over the change.
class EngineSet {
public:
	/// @brief The engine of @p mode's row; the first row's for a mode no row names.
	[[nodiscard]] Engine& for_mode(SynthMode mode) {
		Engine* chosen = &std::get<0>(engines_);
		(void)find_row<EngineRows>([&]<class Row>(Row) {
			if (Row::kMode != mode) {
				return false;
			}
			chosen = &of<Row>();
			return true;
		});
		return *chosen;
	}
	/// @brief The engine of @p Row.
	template <class Row>
	[[nodiscard]] Row::Engine& of() {
		return std::get<typename Row::Engine>(engines_);
	}
	/// @brief FM, whose modulator levels the voice keeps current in every mode.
	[[nodiscard]] FMEngine& fm() { return of<FmRow>(); }

private:
	detail::EnginesOf<EngineRows>::type engines_;
};

} // namespace deluge::synth
