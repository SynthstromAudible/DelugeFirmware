#include "instruments/classic/codec.h"
#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/state_access.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"
#include "support/printers/plugin_result_printer.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

using namespace deluge::instruments::classic;

namespace {

/// A new song's codec context, whose input tick magnitude is 0.
constexpr deluge::plugin::CodecContext kNewSong{.input_tick_magnitude = 0};

/// A patch whose byte i is i, as the 312 upper-case hex digits a file writes.
std::string patchHex() {
	std::string hex;
	for (int32_t i = 0; i < 156; i++) {
		hex += std::format("{:02X}", i);
	}
	return hex;
}

/// The patch patchHex() writes.
std::array<uint8_t, 156> patchBytes() {
	std::array<uint8_t, 156> bytes{};
	for (int32_t i = 0; i < 156; i++) {
		bytes[i] = static_cast<uint8_t>(i);
	}
	return bytes;
}

/// The sound an XML document holds, decoded into a new synth's state; none when it does not read.
std::optional<SoundState> fromXml(const std::string& xml) {
	auto doc = deluge::serialization::parseXml(xml);
	SoundState state;
	if (!doc || !decode(*doc->find("sound"), state, kNewSong)) {
		return std::nullopt;
	}
	return state;
}

/// A sound holding a DX7 with patchHex(), engine mode 2, random detune 5, transposed +3 and −7 cents, on @p slot, in
/// @p mode, the other slot a square.
std::string dx7Sound(const std::string& mode, const std::string& slot) {
	const std::string dx =
	    R"(type="dx7" transpose="3" cents="-7" dx7patch=")" + patchHex() + R"(" dx7enginemode="2" dx7randomdetune="5")";
	const std::string other = R"(type="square")";
	return "<sound mode=\"" + mode + "\"><osc1 " + (slot == "osc1" ? dx : other) + "/><osc2 "
	       + (slot == "osc2" ? dx : other) + "/></sound>";
}

/// Whether slot @p id of @p state holds a DX7 patch.
bool hasPatch(const SoundState& state, SourceId id) {
	return dx_patch(state, id) != nullptr;
}

} // namespace

// clang-format off
describe dx7_forms("a sound's oscillator-slot DX7 in every form it was saved in", ${
	it("reads a subtractive sound's DX7 on the first slot with its patch, engine mode, detune and tuning", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound("subtractive", "osc1"));
		expect(state.has_value()).to_equal(true);
		expect(synth_mode(*state)).to_equal(SynthMode::SUBTRACTIVE);
		expect(source_type(*state, SourceId::A)).to_equal(OscType::DX7);
		expect(source_type(*state, SourceId::B)).to_equal(OscType::SQUARE);
		const DxPatchState* patch = dx_patch(*state, SourceId::A);
		expect(patch).not_().to_equal(static_cast<const DxPatchState*>(nullptr));
		expect(patch->params).to_equal(patchBytes());
		expect(int{patch->engine_mode}).to_equal(2);
		expect(patch->random_detune).to_equal(5);
		expect(int{source_tuning(*state, SourceId::A).transpose}).to_equal(3);
		expect(int{source_tuning(*state, SourceId::A).cents}).to_equal(-7);
		expect(hasPatch(*state, SourceId::B)).to_equal(false);
	});

	it("gives a DX7 saved without a patch the init voice", _ {
		const std::optional<SoundState> state =
		    fromXml(R"(<sound mode="subtractive"><osc1 type="dx7"/><osc2 type="square"/></sound>)");
		expect(state.has_value()).to_equal(true);
		const DxPatchState* patch = dx_patch(*state, SourceId::A);
		expect(patch).not_().to_equal(static_cast<const DxPatchState*>(nullptr));
		expect(patch->params).to_equal(kDx7InitVoice);
		expect(int{patch->engine_mode}).to_equal(0);
		expect(patch->random_detune).to_equal(0);
	});

	it("reads a DX7 on the second slot as that slot's", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound("subtractive", "osc2"));
		expect(state.has_value()).to_equal(true);
		expect(source_type(*state, SourceId::A)).to_equal(OscType::SQUARE);
		expect(source_type(*state, SourceId::B)).to_equal(OscType::DX7);
		expect(hasPatch(*state, SourceId::B)).to_equal(true);
		expect(dx_patch(*state, SourceId::B)->params).to_equal(patchBytes());
		expect(int{source_tuning(*state, SourceId::B).transpose}).to_equal(3);
	});

	it("reads a ring-modulated sound's DX7 as a wavetable, keeping its patch", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound("ringmod", "osc1"));
		expect(state.has_value()).to_equal(true);
		expect(synth_mode(*state)).to_equal(SynthMode::RINGMOD);
		expect(source_type(*state, SourceId::A)).to_equal(OscType::WAVETABLE);
		expect(hasPatch(*state, SourceId::A)).to_equal(true);
	});

	it("reads an FM sound's DX7 as a sine, keeping its patch", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound("fm", "osc1"));
		expect(state.has_value()).to_equal(true);
		expect(synth_mode(*state)).to_equal(SynthMode::FM);
		expect(source_type(*state, SourceId::A)).to_equal(OscType::SINE);
		expect(hasPatch(*state, SourceId::A)).to_equal(true);
	});

	it("reads the JSON form as the XML form", _ {
		auto doc = deluge::serialization::parseJson(
		    R"({"sound": {"mode": "subtractive", "osc1": {"type": "dx7", "transpose": 3, "cents": -7, "dx7patch": ")"
		    + patchHex() + R"(", "dx7enginemode": 2, "dx7randomdetune": 5}, "osc2": {"type": "square"}}})");
		expect(doc.has_value()).to_equal(true);
		SoundState json;
		expect(decode(*doc->find("sound"), json, kNewSong)).to_have_value();
		const std::optional<SoundState> xml = fromXml(dx7Sound("subtractive", "osc1"));
		expect(deluge::serialization::first_difference(dump_state(json), dump_state(*xml))).to_equal(std::string{});
	});
});
// clang-format on

CPPSPEC_SPEC(dx7_forms)
