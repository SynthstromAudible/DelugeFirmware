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
#include "synth/sources/source_slot.h"
#include "synth/sources/source_table.h"

#include "fake_voice_ports.h"

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

/// The source a slot of @p type plays in @p mode, written out from the rule itself: a port-backed source (sample,
/// live input, DX7) outside FM, else a wave the subtractive and ring-mod engines build, else none.
std::string expected_source(OscType type, SynthMode mode) {
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
	const bool va = type == OscType::SINE || type == OscType::TRIANGLE || type == OscType::SQUARE
	                || type == OscType::ANALOG_SQUARE || type == OscType::SAW || type == OscType::ANALOG_SAW_2;
	if (engine_mode && va) {
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
std::string chosen_by_table(OscType type, SynthMode mode) {
	std::string chosen = "none";
	const auto pick = [&]<class Kind>(Kind) {
		if (!Kind::plays(type)) {
			return false;
		}
		chosen = name(Kind{});
		return true;
	};
	if (holds_oscillators(mode) && find_row<PortedKinds>(pick)) {
		return chosen;
	}
	if (holds_oscillators(mode)) {
		find_row<HeldKinds>(pick);
	}
	return chosen;
}

/// How many rows play @p type.
int32_t rows_playing(OscType type) {
	int32_t rows = 0;
	for_each_row<SourceKinds>([&]<class Kind>(Kind) { rows += Kind::plays(type) ? 1 : 0; });
	return rows;
}

static_assert(
    std::is_same_v<HeldOscillator, std::variant<std::monostate, VirtualAnalogSource, WavetableSource, SupersawSource>>,
    "the held oscillator is the held rows' sources, in the table's order");
static_assert(std::is_same_v<PortedSources, std::tuple<SampleSource, DX7Source, InputSource>>,
              "the ported sources are the ported rows', in the order bind and unassign walk them");

/// A sample port whose note-on and loop-end release answer as set, counting each call.
class AnsweringSamplePort final : public test::FakeSamplePort {
public:
	bool starts{true};      ///< What note_on answers
	bool keeps_going{true}; ///< What note_off_at_loop_end answers
	int32_t note_ons{};
	int32_t releases{};
	bool note_on(uint32_t samples_late, int32_t priority_rating) override {
		++note_ons;
		(void)test::FakeSamplePort::note_on(samples_late, priority_rating);
		return starts;
	}
	bool note_off_at_loop_end(int32_t /*priority_rating*/) override {
		++releases;
		return keeps_going;
	}
};

/// A slot bound to @p port, with the carrier feedback set so a reset shows.
void bind_with_feedback(SourceSlot& slot, AnsweringSamplePort& port) {
	slot.bind({.sample = &port});
	slot.carrier_feedback = deluge::dsp::Q<31>::from_raw(12345);
}

/// Starts @p slot on a note of @p type in @p mode, a new voice (every reset), free-running.
bool start(SourceSlot& slot, OscType type, SynthMode mode, deluge::dsp::fixed::Cong& noise) {
	return slot.note_on(SourceSetup{.osc_type = type}, 0, kFreeRunningPhase, true, mode, 60, 100, 0, noise);
}

} // namespace

// clang-format off
describe source_table_spec_body("the source table", ${
	it("chooses each type's source in every mode: a port-backed one outside FM, else a wave the engines build", _{
		for (const SynthMode mode : kModes) {
			for (const OscType type : kTypes) {
				expect(std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(type),
				                   chosen_by_table(type, mode)))
				    .to_equal(std::format("{} {} {}", std::to_underlying(mode), std::to_underlying(type),
				                          expected_source(type, mode)));
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

	it("starts a sample through its port, answering as the port does, and leaves the carrier feedback alone", _{
		for (const bool starts : {true, false}) {
			AnsweringSamplePort port;
			port.starts = starts;
			SourceSlot slot;
			bind_with_feedback(slot, port);
			deluge::dsp::fixed::Cong noise{7};
			expect(start(slot, OscType::SAMPLE, SynthMode::SUBTRACTIVE, noise)).to_equal(starts);
			expect(port.note_ons).to_equal(1);
			expect(slot.carrier_feedback.raw()).to_equal(12345);
			expect(noise.state()).to_equal(uint32_t{7});
		}
	});

	it("builds no source for a sample in FM, and resets the carrier feedback on a new voice", _{
		AnsweringSamplePort port;
		SourceSlot slot;
		bind_with_feedback(slot, port);
		deluge::dsp::fixed::Cong noise{7};
		expect(start(slot, OscType::SAMPLE, SynthMode::FM, noise)).to_equal(true);
		expect(port.note_ons).to_equal(0);
		expect(std::holds_alternative<std::monostate>(slot.oscillator())).to_equal(true);
		expect(slot.carrier_feedback.raw()).to_equal(0);
	});

	it("builds a wave for a subtractive note, drawing noise only for a free-running supersaw's side saws, one draw each", _{
		AnsweringSamplePort port;
		SourceSlot va;
		bind_with_feedback(va, port);
		deluge::dsp::fixed::Cong noise{7};
		expect(start(va, OscType::SAW, SynthMode::SUBTRACTIVE, noise)).to_equal(true);
		expect(std::holds_alternative<VirtualAnalogSource>(va.oscillator())).to_equal(true);
		expect(noise.state()).to_equal(uint32_t{7});

		SourceSlot supersaw;
		bind_with_feedback(supersaw, port);
		deluge::dsp::fixed::Cong drawn{7};
		for (size_t i = 0; i < std::tuple_size_v<deluge::dsp::fixed::Supersaw::SidePhases>; i++) {
			(void)drawn.next();
		}
		expect(start(supersaw, OscType::SUPERSAW, SynthMode::SUBTRACTIVE, noise)).to_equal(true);
		expect(std::holds_alternative<SupersawSource>(supersaw.oscillator())).to_equal(true);
		expect(noise.state()).to_equal(drawn.state());
	});

	it("releases a sample only while it sounds, and lets go of the slot when its release ends the note", _{
		AnsweringSamplePort port;
		port.keeps_going = false;
		SourceSlot slot;
		bind_with_feedback(slot, port);
		slot.release(OscType::SAMPLE, {.priority_rating = 3});
		expect(port.releases).to_equal(0);
		slot.active = true;
		slot.release(OscType::SAMPLE, {.priority_rating = 3});
		expect(port.releases).to_equal(1);
		expect(slot.active).to_equal(false);
		slot.active = true;
		slot.release(OscType::SAW, {.priority_rating = 3});
		expect(slot.active).to_equal(true);
	});

	it("gives each type to at most one row", _{
		for (const OscType type : kTypes) {
			expect(rows_playing(type)).to_be_less_than(int32_t{2});
		}
	});
});
// clang-format on

DELUGE_SPEC(source_table_spec, source_table_spec_body)
