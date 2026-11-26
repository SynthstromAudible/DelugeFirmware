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
#include "synth/engines/engine_state.h"
#include "synth/table_rows.h"
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

/// @file
/// @brief The engines a sound renders with, one row each: the voice's engine set and its lookup by mode, the saved
/// engine state, and which modes play the oscillator slots, are generated from these rows. Adding an engine is adding
/// a row (and its word in the instrument's `words.h`).

namespace deluge::synth {

class SubtractiveEngine;
class RingModEngine;
class FMEngine;
class Dx7Engine;

/// @brief Subtractive: the two slots' sources mixed into the filters.
struct SubtractiveRow {
	using Engine = SubtractiveEngine;
	using State = SubtractiveState;
	static constexpr SynthMode kMode = SynthMode::SUBTRACTIVE;
	static constexpr bool kHoldsOscillators = true; ///< its state is the two oscillator slots
};

/// @brief Ring modulation: the two slots' waves multiplied.
struct RingModRow {
	using Engine = RingModEngine;
	using State = RingModState;
	static constexpr SynthMode kMode = SynthMode::RINGMOD;
	static constexpr bool kHoldsOscillators = true; ///< its state is the two oscillator slots
};

/// @brief FM: the slots are carriers under two modulators; their sources park.
struct FmRow {
	using Engine = FMEngine;
	using State = FmState;
	static constexpr SynthMode kMode = SynthMode::FM;
	static constexpr bool kHoldsOscillators = false; ///< its carriers keep only the slots' pitch
};

/// @brief DX7: one DX7 voice per unison part on the subtractive chain; its state is the patch and the DX7's pitch.
struct Dx7Row {
	using Engine = Dx7Engine;
	using State = Dx7State;
	static constexpr SynthMode kMode = SynthMode::DX7;
	static constexpr bool kHoldsOscillators = false; ///< it plays no oscillator slot's source
};

/// @brief The engines, in the saved variant's order. The first is the fallback for a mode no row names.
using EngineRows = std::tuple<SubtractiveRow, RingModRow, FmRow, Dx7Row>;

namespace detail {

template <class Rows>
struct StatesOf;

template <class... Rows>
struct StatesOf<std::tuple<Rows...>> {
	using type = std::variant<typename Rows::State...>;
};

/// @brief The row a dual-oscillator state falls back to: the first, which holds the oscillators.
using DualFallbackRow = std::tuple_element_t<0, EngineRows>;
static_assert(DualFallbackRow::kHoldsOscillators);

/// @brief The oscillators of whichever oscillator-holding row @p state is; null for none.
template <class Dual, class State>
Dual* oscillators_in(State& state) noexcept {
	Dual* found = nullptr;
	(void)find_row<EngineRows>([&]<class Row>(Row) {
		if constexpr (Row::kHoldsOscillators) {
			if (auto* held = std::get_if<typename Row::State>(&state)) {
				found = &held->oscillators;
				return true;
			}
		}
		return false;
	});
	return found;
}

} // namespace detail

/// @brief The engine a sound renders with, and what it saves: one alternative per row, in row order.
using EngineState = detail::StatesOf<EngineRows>::type;

/// @brief Whether @p mode's engine plays the two oscillator slots' sources: a held wave is built, and a ported source
/// sounds, only then.
/// @return True for subtractive and ring-mod.
[[nodiscard]] constexpr bool holds_oscillators(SynthMode mode) noexcept {
	return find_row<EngineRows>([&]<class Row>(Row) { return Row::kMode == mode && Row::kHoldsOscillators; });
}

/// @brief The mode whose engine @p state is.
/// @return The row's mode.
[[nodiscard]] constexpr SynthMode mode_of(const EngineState& state) noexcept {
	SynthMode mode = std::tuple_element_t<0, EngineRows>::kMode;
	(void)find_row<EngineRows>([&]<class Row>(Row) {
		if (!std::holds_alternative<typename Row::State>(state)) {
			return false;
		}
		mode = Row::kMode;
		return true;
	});
	return mode;
}

/// @brief The two oscillator slots @p state plays.
/// @return Null when its engine keeps none (FM).
[[nodiscard]] inline DualSourceState* oscillators_of(EngineState& state) noexcept {
	return detail::oscillators_in<DualSourceState>(state);
}

/// @copydoc oscillators_of(EngineState&)
[[nodiscard]] inline const DualSourceState* oscillators_of(const EngineState& state) noexcept {
	return detail::oscillators_in<const DualSourceState>(state);
}

/// @brief @p dual played by @p mode's engine; a mode whose engine keeps no oscillators plays them as the first row.
/// @param mode The synth mode.
/// @param dual The two slots.
/// @return The engine state.
[[nodiscard]] inline EngineState dual_state_for(SynthMode mode, DualSourceState dual) {
	std::optional<EngineState> state;
	(void)find_row<EngineRows>([&]<class Row>(Row) {
		if constexpr (Row::kHoldsOscillators) {
			if (Row::kMode == mode) {
				state.emplace(typename Row::State{.oscillators = std::move(dual)});
				return true;
			}
		}
		return false;
	});
	if (!state) {
		state.emplace(detail::DualFallbackRow::State{.oscillators = std::move(dual)});
	}
	return *std::move(state);
}

} // namespace deluge::synth
