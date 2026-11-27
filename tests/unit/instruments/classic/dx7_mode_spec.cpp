#include "instruments/classic/codec.h"
#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/flat_layout.h"
#include "instruments/classic/record_edit.h"
#include "instruments/classic/state_access.h"
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

/// A DX7-mode sound: patchHex(), engine mode 2, random detune 5, transposed +3 and −7 cents, retriggered at 0x100.
std::string dx7Sound() {
	return R"(<sound mode="dx7"><dx7 transpose="3" cents="-7" retrigPhase="256" patch=")" + patchHex()
	       + R"(" engineMode="2" randomDetune="5"/></sound>)";
}

/// A subtractive sound whose slots and sync a switch must keep: a saw tuned and retriggered, then a wavetable synced.
std::string subtractiveSound() {
	return R"(<sound mode="subtractive"><osc1 type="saw" transpose="5" cents="9" retrigPhase="1000"/>)"
	       R"(<osc2 type="wavetable" transpose="-2" oscillatorSync="1" fileName="WAVETABLES/a.wav"/></sound>)";
}

/// @p state's first difference from @p other as their dumps show it, or empty when they are the same record.
std::string difference(const SoundState& state, const SoundState& other) {
	if (state == other) {
		return {};
	}
	const std::string dumped = deluge::serialization::first_difference(dump_state(state), dump_state(other));
	return dumped.empty() ? std::string{"the records differ in what they keep"} : dumped;
}

} // namespace

// clang-format off
describe dx7_mode("a sound in DX7 mode", ${
	it("reads its DX7 engine's patch and pitch from its dx7 entry, its slots parked as a new synth's", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound());
		expect(state.has_value()).to_equal(true);
		expect(synth_mode(*state)).to_equal(SynthMode::DX7);
		const DxPatchState* patch = dx7_patch(*state);
		expect(patch).not_().to_equal(static_cast<const DxPatchState*>(nullptr));
		expect(patch->params).to_equal(patchBytes());
		expect(int{patch->engine_mode}).to_equal(2);
		expect(patch->random_detune).to_equal(5);
		expect(int{dx7_pitch(*state).tuning.transpose}).to_equal(3);
		expect(int{dx7_pitch(*state).tuning.cents}).to_equal(-7);
		expect(phase_word(dx7_pitch(*state).retrigger)).to_equal(256U);
		expect(source_type(*state, SourceId::A)).to_equal(OscType::SQUARE);
		expect(source_type(*state, SourceId::B)).to_equal(OscType::SQUARE);
	});

	it("gives a DX7 read without a patch the init voice", _ {
		const std::optional<SoundState> state = fromXml(R"(<sound mode="dx7"/>)");
		expect(state.has_value()).to_equal(true);
		const DxPatchState* patch = dx7_patch(*state);
		expect(patch).not_().to_equal(static_cast<const DxPatchState*>(nullptr));
		expect(patch->params).to_equal(kDx7InitVoice);
	});

	it("writes its dx7 entry and no oscillators, and reads back to the same record", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound());
		expect(state.has_value()).to_equal(true);
		const deluge::serialization::Value written = encode(*state, kNewSong);
		expect(written.find("dx7")).not_().to_equal(static_cast<const deluge::serialization::Value*>(nullptr));
		expect(written.find("osc1")).to_equal(static_cast<const deluge::serialization::Value*>(nullptr));
		expect(written.find("osc2")).to_equal(static_cast<const deluge::serialization::Value*>(nullptr));
		SoundState read;
		expect(decode(written, read, kNewSong)).to_have_value();
		expect(difference(read, *state)).to_equal(std::string{});
	});

	it("flattens to the file's layout and composes back to the same record", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound());
		expect(state.has_value()).to_equal(true);
		expect(difference(to_record(to_flat(*state)), *state)).to_equal(std::string{});
		SoundState switched = *state;
		set_engine(switched, SynthMode::SUBTRACTIVE);
		expect(difference(to_record(to_flat(switched)), switched)).to_equal(std::string{});
	});

	it("writes no dx7 entry in another mode", _ {
		const std::optional<SoundState> state = fromXml(subtractiveSound());
		expect(state.has_value()).to_equal(true);
		expect(encode(*state, kNewSong).find("dx7")).to_equal(static_cast<const deluge::serialization::Value*>(nullptr));
	});

	it("keeps its DX7 through subtractive and back", _ {
		const std::optional<SoundState> state = fromXml(dx7Sound());
		expect(state.has_value()).to_equal(true);
		SoundState switched = *state;
		set_engine(switched, SynthMode::SUBTRACTIVE);
		expect(synth_mode(switched)).to_equal(SynthMode::SUBTRACTIVE);
		expect(dx7_patch(switched)->params).to_equal(patchBytes());
		set_engine(switched, SynthMode::DX7);
		expect(difference(switched, *state)).to_equal(std::string{});
	});

	it("keeps the slots, their pitches and the sync through the DX7 and back", _ {
		const std::optional<SoundState> state = fromXml(subtractiveSound());
		expect(state.has_value()).to_equal(true);
		SoundState switched = *state;
		set_engine(switched, SynthMode::DX7);
		expect(synth_mode(switched)).to_equal(SynthMode::DX7);
		expect(int{source_tuning(switched, SourceId::A).transpose}).to_equal(5);
		expect(oscillator_sync(switched)).to_equal(true);
		set_engine(switched, SynthMode::SUBTRACTIVE);
		expect(difference(switched, *state)).to_equal(std::string{});
	});

	it("keeps FM's state through the DX7 and back", _ {
		const std::optional<SoundState> state = fromXml(
		    R"(<sound mode="fm"><osc1 transpose="4"/><modulator1 transpose="7"/><modulator2 toModulator1="1"/></sound>)");
		expect(state.has_value()).to_equal(true);
		SoundState switched = *state;
		set_engine(switched, SynthMode::DX7);
		set_engine(switched, SynthMode::FM);
		expect(difference(switched, *state)).to_equal(std::string{});
	});

	it("edits a parked slot, which a switch back plays", _ {
		std::optional<SoundState> state = fromXml(subtractiveSound());
		expect(state.has_value()).to_equal(true);
		set_engine(*state, SynthMode::DX7);
		SlotEdit edit = begin_slot_edit(*state, 0);
		edit.source.transpose = 11;
		end_slot_edit(*state, 0, edit);
		expect(int{source_tuning(*state, SourceId::A).transpose}).to_equal(11);
		set_engine(*state, SynthMode::SUBTRACTIVE);
		expect(int{source_tuning(*state, SourceId::A).transpose}).to_equal(11);
		expect(source_type(*state, SourceId::A)).to_equal(OscType::SAW);
	});
});
// clang-format on

CPPSPEC_SPEC(dx7_mode)
