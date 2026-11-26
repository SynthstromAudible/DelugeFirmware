/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "synth/engines/engine_table.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "support/spec_main.h"

using namespace deluge::synth;
using deluge::instruments::classic::parse_synth_mode;
using deluge::instruments::classic::to_string;

namespace {

constexpr std::array kModes{SynthMode::SUBTRACTIVE, SynthMode::FM, SynthMode::RINGMOD};
/// A value no row names.
constexpr auto kUnnamedMode = static_cast<SynthMode>(200);

/// How many rows name @p mode.
int32_t rows_naming(SynthMode mode) {
	int32_t rows = 0;
	for_each_row<EngineRows>([&]<class Row>(Row) { rows += Row::kMode == mode ? 1 : 0; });
	return rows;
}

/// Two slots that differ from the defaults, so a copy shows.
DualSourceState a_dual() {
	return {.slots = {VaState{}, WavetableState{}}, .sync = true};
}

} // namespace

static_assert(std::is_same_v<EngineState, std::variant<SubtractiveState, RingModState, FmState>>,
              "the saved engine state is the rows' states, in today's order");

// clang-format off
describe engine_table_spec_body("the engine table", ${
	it("names every synth mode in exactly one row", _{
		for (const SynthMode mode : kModes) {
			expect(rows_naming(mode)).to_equal(int32_t{1});
		}
		expect(int32_t{std::tuple_size_v<EngineRows>}).to_equal(int32_t{kModes.size()});
	});

	it("reads a state's mode from its row", _{
		expect(mode_of(SubtractiveState{})).to_equal(SynthMode::SUBTRACTIVE);
		expect(mode_of(RingModState{})).to_equal(SynthMode::RINGMOD);
		expect(mode_of(FmState{})).to_equal(SynthMode::FM);
	});

	it("plays the oscillator slots in subtractive and ring-mod only", _{
		expect(holds_oscillators(SynthMode::SUBTRACTIVE)).to_equal(true);
		expect(holds_oscillators(SynthMode::RINGMOD)).to_equal(true);
		expect(holds_oscillators(SynthMode::FM)).to_equal(false);
		expect(holds_oscillators(kUnnamedMode)).to_equal(false);
	});

	it("finds the oscillators of a state that holds them, and none in FM", _{
		EngineState sub{SubtractiveState{.oscillators = a_dual()}};
		expect(oscillators_of(sub) == &std::get<SubtractiveState>(sub).oscillators).to_equal(true);
		EngineState ring{RingModState{.oscillators = a_dual()}};
		expect(oscillators_of(ring) == &std::get<RingModState>(ring).oscillators).to_equal(true);
		const EngineState& const_ring = ring;
		expect(oscillators_of(const_ring) == &std::get<RingModState>(ring).oscillators).to_equal(true);
		EngineState fm{FmState{}};
		expect(oscillators_of(fm) == nullptr).to_equal(true);
		const EngineState& const_fm = fm;
		expect(oscillators_of(const_fm) == nullptr).to_equal(true);
	});

	it("plays two slots as the mode's engine, and as subtractive where the mode keeps none", _{
		expect(dual_state_for(SynthMode::RINGMOD, a_dual()) == EngineState{RingModState{.oscillators = a_dual()}})
		    .to_equal(true);
		expect(dual_state_for(SynthMode::SUBTRACTIVE, a_dual())
		       == EngineState{SubtractiveState{.oscillators = a_dual()}})
		    .to_equal(true);
		expect(dual_state_for(SynthMode::FM, a_dual()) == EngineState{SubtractiveState{.oscillators = a_dual()}})
		    .to_equal(true);
		expect(dual_state_for(kUnnamedMode, a_dual()) == EngineState{SubtractiveState{.oscillators = a_dual()}})
		    .to_equal(true);
	});

	it("saves each row's mode under a word of its own that reads back as it", _{
		std::array<std::string_view, std::tuple_size_v<EngineRows>> words{};
		size_t i = 0;
		for_each_row<EngineRows>([&]<class Row>(Row) {
			words[i++] = to_string(Row::kMode);
			expect(parse_synth_mode(to_string(Row::kMode))).to_equal(Row::kMode);
		});
		expect(words[0] != words[1] && words[1] != words[2] && words[0] != words[2]).to_equal(true);
	});
});
// clang-format on

DELUGE_SPEC(engine_table_spec, engine_table_spec_body)
