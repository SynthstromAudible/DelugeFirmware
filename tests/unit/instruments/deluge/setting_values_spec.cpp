#include "instruments/deluge/codec.h"
#include "instruments/deluge/settings.h"
#include "serialization/tree.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::plugin::SettingKind;
using deluge::serialization::Value;

namespace {

SoundState fmState() {
	return deluge::test::with(SoundState{}, "mode", to_string(SynthMode::FM));
}

SoundState sampleState() {
	SoundState state = deluge::test::with_kind(SoundState{}, SourceId::A, OscType::SAMPLE);
	return with_ranges(std::move(state), SourceId::A,
	                   {deluge::synth::SourceRange{.top_note = 32767, .file_path = "SAMPLES/x.wav"}});
}

SoundState wavetableState() {
	SoundState state = deluge::test::with_kind(SoundState{}, SourceId::A, OscType::WAVETABLE);
	return with_ranges(std::move(state), SourceId::A,
	                   {deluge::synth::SourceRange{.top_note = 32767, .file_path = "WAVETABLES/x.wav"}});
}

/// The first non-structured row whose answer for @p state is one check_setting refuses, or empty.
std::string firstUncheckedAnswer(const SoundState& state) {
	for (const deluge::plugin::SettingInfo& row : kSettings) {
		if (row.kind == SettingKind::STRUCTURED) {
			continue;
		}
		if (const std::optional<Value> value = setting_value(state, row.path)) {
			if (!check_setting(row.path, *value)) {
				return std::string(row.path) + "=" + value->str();
			}
		}
	}
	return {};
}

/// @p path's answer for @p state as its text, or "none".
std::string answer(const SoundState& state, std::string_view path) {
	const std::optional<Value> value = setting_value(state, path);
	return value ? value->str() : "none";
}

} // namespace

// clang-format off
describe setting_values("a sound's settings in an Edit's form", ${
	it("answers only values the schema allows", _ {
		expect(firstUncheckedAnswer(SoundState{})).to_equal(std::string{});
		expect(firstUncheckedAnswer(fmState())).to_equal(std::string{});
		expect(firstUncheckedAnswer(sampleState())).to_equal(std::string{});
		expect(firstUncheckedAnswer(wavetableState())).to_equal(std::string{});
	});

	it("answers nothing where a sound does not take the setting", _ {
		expect(answer(SoundState{}, "osc1/loopMode")).to_equal(std::string("none"));
		expect(answer(sampleState(), "osc1/transpose")).to_equal(std::string("none"));
		expect(answer(SoundState{}, "modulator1/transpose")).to_equal(std::string("none"));
		expect(answer(fmState(), "osc1/type")).to_equal(std::string("none"));
		expect(answer(fmState(), "osc1/loopMode")).to_equal(std::string("none"));
		expect(answer(SoundState{}, "osc1/sampleRanges")).to_equal(std::string("none"));
	});

	it("answers where it does", _ {
		expect(answer(sampleState(), "osc1/loopMode")).to_equal(std::string("0"));
		expect(answer(fmState(), "osc1/transpose")).to_equal(std::string("0"));
		expect(answer(fmState(), "modulator2/transpose")).to_equal(std::string("-12"));
		expect(answer(wavetableState(), "osc1/type")).to_equal(std::string("wavetable"));
	});

	it("takes no setting its sound does not hold", _ {
		expect(takes_setting(fmState(), "osc1/type")).to_be_false();
		expect(takes_setting(SoundState{}, "osc1/sampleRanges")).to_be_false();
	});

	it("gives an LFO's sync level as the level itself", _ {
		const SoundState state = deluge::test::with(SoundState{}, "lfo1/syncLevel", "3");
		expect(answer(state, "lfo1/syncLevel")).to_equal(std::string("3"));
	});
});

CPPSPEC_SPEC(setting_values)
