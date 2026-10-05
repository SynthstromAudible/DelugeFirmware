#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::instrument;
using deluge::engine::SettingKind;
using deluge::serialization::Value;

namespace {

SoundState fmState() {
	SoundState state = new_synth_state();
	state.synth_mode = SynthMode::FM;
	return state;
}

SoundState sampleState() {
	SoundState state = new_synth_state();
	state.sources[0].osc_type = OscType::SAMPLE;
	state.sources[0].ranges.push_back(RangeState{.top_note = 32767, .file_path = "SAMPLES/x.wav"});
	return state;
}

SoundState wavetableState() {
	SoundState state = new_synth_state();
	state.sources[0].osc_type = OscType::WAVETABLE;
	state.sources[0].ranges.push_back(RangeState{.top_note = 32767, .file_path = "WAVETABLES/x.wav"});
	return state;
}

/// The first non-structured row whose answer for @p state is one check_setting refuses, or empty.
std::string firstUncheckedAnswer(const SoundState& state) {
	for (const deluge::engine::SettingInfo& row : kSettings) {
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
		expect(firstUncheckedAnswer(new_synth_state())).to_equal(std::string{});
		expect(firstUncheckedAnswer(fmState())).to_equal(std::string{});
		expect(firstUncheckedAnswer(sampleState())).to_equal(std::string{});
		expect(firstUncheckedAnswer(wavetableState())).to_equal(std::string{});
	});

	it("answers nothing where a sound does not take the setting", _ {
		expect(answer(new_synth_state(), "osc1/loopMode")).to_equal(std::string("none"));
		expect(answer(sampleState(), "osc1/transpose")).to_equal(std::string("none"));
		expect(answer(new_synth_state(), "modulator1/transpose")).to_equal(std::string("none"));
		expect(answer(fmState(), "osc1/type")).to_equal(std::string("none"));
		expect(answer(fmState(), "osc1/loopMode")).to_equal(std::string("none"));
		expect(answer(new_synth_state(), "osc1/sampleRanges")).to_equal(std::string("none"));
	});

	it("answers where it does", _ {
		expect(answer(sampleState(), "osc1/loopMode")).to_equal(std::string("0"));
		expect(answer(fmState(), "osc1/transpose")).to_equal(std::string("0"));
		expect(answer(fmState(), "modulator2/transpose")).to_equal(std::string("-12"));
		expect(answer(wavetableState(), "osc1/type")).to_equal(std::string("wavetable"));
		expect(answer(new_synth_state(), "midiOutput/channel")).to_equal(std::string("255"));
	});

	it("takes an unset drum note, which answers nothing", _ {
		expect(answer(new_synth_state(), "midiOutput/noteForDrum")).to_equal(std::string("none"));
		expect(takes_setting(new_synth_state(), "midiOutput/noteForDrum")).to_equal(true);
		expect(takes_setting(fmState(), "osc1/type")).to_equal(false);
		expect(takes_setting(new_synth_state(), "osc1/sampleRanges")).to_equal(false);
	});

	it("gives an LFO's sync level as the level itself", _ {
		SoundState state = new_synth_state();
		state.lfo_sync_level[0] = static_cast<deluge::engine::SyncLevel>(3);
		expect(answer(state, "lfo1/syncLevel")).to_equal(std::string("3"));
	});
});

CPPSPEC_SPEC(setting_values)
