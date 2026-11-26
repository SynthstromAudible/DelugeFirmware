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
#include "synth/sources/source_table.h"

#include <array>
#include <cppspec.hpp>
#include <format>
#include <string>
#include <type_traits>
#include <variant>

#include "support/spec_main.h"

using namespace deluge::synth;

namespace {

constexpr std::array kTypes{OscType::SINE,        OscType::TRIANGLE,     OscType::SQUARE,   OscType::ANALOG_SQUARE,
                            OscType::SAW,         OscType::ANALOG_SAW_2, OscType::SUPERSAW, OscType::WAVETABLE,
                            OscType::SAMPLE,      OscType::DX7,          OscType::INPUT_L,  OscType::INPUT_R,
                            OscType::INPUT_STEREO};
constexpr std::array kModes{SynthMode::SUBTRACTIVE, SynthMode::FM, SynthMode::RINGMOD};

std::string name(SampleKind) {
	return "sample";
}
std::string name(Dx7Kind) {
	return "dx7";
}
std::string name(InputKind) {
	return "input";
}
std::string name(VirtualAnalogKind) {
	return "va";
}
std::string name(WavetableKind) {
	return "wavetable";
}
std::string name(SupersawKind) {
	return "supersaw";
}

/// The source a slot of @p type plays in @p mode, as a slot's note-on decided it before the table: a port-backed
/// source outside FM, else a wave the subtractive and ring-mod engines build, else none.
std::string decidedBefore(OscType type, SynthMode mode) {
	if (mode != SynthMode::FM && type == OscType::SAMPLE) {
		return "sample";
	}
	if (mode != SynthMode::FM
	    && (type == OscType::INPUT_L || type == OscType::INPUT_R || type == OscType::INPUT_STEREO)) {
		return "input";
	}
	if (mode != SynthMode::FM && type == OscType::DX7) {
		return "dx7";
	}
	const bool engine_mode = mode == SynthMode::SUBTRACTIVE || mode == SynthMode::RINGMOD;
	if (engine_mode && va_shape::is_virtual_analog(type)) {
		return "va";
	}
	if (engine_mode && type == OscType::WAVETABLE) {
		return "wavetable";
	}
	if (engine_mode && type == OscType::SUPERSAW) {
		return "supersaw";
	}
	return "none";
}

/// The source the table's rows choose: the first ported row playing @p type outside FM, else the first held row in
/// the engines that build one, else none.
std::string decidedByTable(OscType type, SynthMode mode) {
	std::string chosen = "none";
	const auto pick = [&]<class Kind>(Kind) {
		if (!Kind::plays(type)) {
			return false;
		}
		chosen = name(Kind{});
		return true;
	};
	if (ported_mode(mode) && find_kind<PortedKinds>(pick)) {
		return chosen;
	}
	if (held_mode(mode)) {
		find_kind<HeldKinds>(pick);
	}
	return chosen;
}

/// How many rows play @p type.
int32_t rowsPlaying(OscType type) {
	int32_t rows = 0;
	for_each_kind<SourceKinds>([&]<class Kind>(Kind) { rows += Kind::plays(type) ? 1 : 0; });
	return rows;
}

static_assert(
    std::is_same_v<HeldOscillator, std::variant<std::monostate, VirtualAnalogSource, WavetableSource, SupersawSource>>,
    "the held oscillator is the held rows' sources, in the table's order");
static_assert(std::is_same_v<PortedSources, std::tuple<SampleSource, DX7Source, InputSource>>,
              "the ported sources are the ported rows', in the order bind and unassign walk them");

} // namespace

// clang-format off
describe source_table_spec_body("the source table", ${
	it("chooses the source a slot played before the table, for every type in every mode", _{
		for (const SynthMode mode : kModes) {
			for (const OscType type : kTypes) {
				expect(std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(type),
				                   decidedByTable(type, mode)))
				    .to_equal(std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(type),
				                          decidedBefore(type, mode)));
			}
		}
	});

	it("plays a sample and a live input at their own rate, outside FM only", _{
		for (const SynthMode mode : kModes) {
			for (const OscType type : kTypes) {
				const bool before =
				    mode != SynthMode::FM
				    && (type == OscType::SAMPLE || type == OscType::INPUT_L || type == OscType::INPUT_R
				        || type == OscType::INPUT_STEREO);
				expect(std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(type),
				                   plays_at_own_rate(type, mode)))
				    .to_equal(std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(type), before));
			}
		}
	});

	it("gives each type to at most one row", _{
		for (const OscType type : kTypes) {
			expect(rowsPlaying(type)).to_be_less_than(int32_t{2});
		}
	});
});
// clang-format on

DELUGE_SPEC(source_table_spec, source_table_spec_body)
