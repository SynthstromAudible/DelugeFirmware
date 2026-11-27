#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/state_access.h"
#include "support/fake_voice_host.h"
#include "support/printers/sound_record_printer.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace deluge::instruments::classic;
using deluge::dsp::fixed::FineTune;

namespace {

/// The text a settings row holds for the enumerator @p value, as a file writes it.
template <typename Enum>
std::string number(Enum value) {
	return std::to_string(std::to_underlying(value));
}

/// An FM sound with every field the config derives from set away from its default.
SoundState fm_state() {
	namespace test = deluge::test;
	SoundState s;
	test::set(s, "mode", to_string(SynthMode::FM));
	// The engine switch turned the filters off; set them on, so a later switch of engine leaves them as they are.
	sound_effects(s).filter.lpf_mode = deluge::effects::FilterMode::TRANSISTOR_24DB;
	sound_effects(s).filter.hpf_mode = deluge::effects::FilterMode::HPLADDER;
	test::set(s, "voicePriority", number(deluge::plugin::VoicePriority::HIGH));
	test::set(s, "polyphonic", to_string(PolyphonyMode::MONO));
	test::set(s, "maxVoices", "3");
	test::set(s, "transpose", "5");
	test::set(s, "cents", "-17");
	test::set(s, "unison/num", "5");
	test::set(s, "unison/detune", "30");
	test::set(s, "unison/spread", "25");
	test::set(s, "osc2/oscillatorSync", "1");
	test::set(s, "modulator2/toModulator1", "1");
	test::set(s, "modulator1/transpose", "7");
	test::set(s, "modulator2/transpose", "-5");
	test::set(s, "modulator1/cents", "50");
	test::set(s, "modulator2/cents", "-50");
	test::set(s, "osc1/retrigPhase", "0");
	test::set(s, "osc2/retrigPhase", std::to_string(1U << 30));
	test::set(s, "modulator1/retrigPhase", "3");
	test::set(s, "modulator2/retrigPhase", std::to_string(static_cast<int32_t>(kFreeRunningPhase)));
	test::set(s, "lfo3/type", to_string(LFOType::SAW));
	test::set(s, "lfo2/syncLevel", number(deluge::plugin::SyncLevel::EIGHTH));
	return s;
}

/// Whether @p a and @p b offset an increment alike.
bool same_tune(const FineTune& a, const FineTune& b) {
	return a.apply(1 << 28) == b.apply(1 << 28);
}

/// One field change and the part it must mark.
struct Change {
	std::string_view name;
	std::function<void(SoundState&)> apply;
	Part part;
};

} // namespace

// clang-format off
describe deluge_synth("the Deluge synth's config and its changed parts", ${
	it("derives every value its voices read from the state", _ {
		const SoundState s = fm_state();
		const deluge::synth::SoundConfig c = DelugeSynth::derive(s);
		expect(c.voicing.priority).to_equal(deluge::plugin::VoicePriority::HIGH);
		expect(c.voicing.polyphony).to_equal(PolyphonyMode::MONO);
		expect(int{c.voicing.max_voices}).to_equal(3);
		expect(int{c.voicing.transpose}).to_equal(5);
		expect(int{c.voicing.cents}).to_equal(-17);
		expect(same_tune(c.voicing.tune, FineTune::of_cents(-17))).to_equal(true);
		expect(c.voicing.unison).to_equal(deluge::synth::derive_unison(5, 30, 25));
		expect(int{c.voicing.unison_detune}).to_equal(30);
		expect(int{c.voicing.unison_spread}).to_equal(25);
		expect(c.engine.mode).to_equal(SynthMode::FM);
		expect(c.engine.osc_sync).to_equal(true);
		expect(c.engine.modulator1_to_modulator0).to_equal(true);
		expect(c.engine.osc_retrigger_phase[1]).to_equal(1U << 30);
		expect(int{c.engine.modulators[0].transpose}).to_equal(7);
		expect(same_tune(c.engine.modulators[1].tune, FineTune::of_cents(-50))).to_equal(true);
		expect(c.engine.modulators[1].retrigger_phase).to_equal(kFreeRunningPhase);
		expect(c.lfos[2].shape).to_equal(LFOType::SAW);
		expect(c.lfos[1].sync_level).to_equal(deluge::plugin::SyncLevel::EIGHTH);
	});

	it("marks nothing when the state is unchanged", _ {
		const SoundState s = fm_state();
		expect(DelugeSynth::changes(s, s).none()).to_equal(true);
	});

	it("marks exactly the part a single field belongs to", _ {
		namespace test = deluge::test;
		const std::vector<Change> changes{
		    {"priority", [](SoundState& s) { test::set(s, "voicePriority", number(deluge::plugin::VoicePriority::LOW)); }, Part::VOICING},
		    {"cents", [](SoundState& s) { test::set(s, "cents", "3"); }, Part::TUNING},
		    {"unison count", [](SoundState& s) { test::set(s, "unison/num", "2"); }, Part::UNISON_COUNT},
		    {"unison detune", [](SoundState& s) { test::set(s, "unison/detune", "1"); }, Part::UNISON_DETUNE},
		    {"unison spread", [](SoundState& s) { test::set(s, "unison/spread", "1"); }, Part::UNISON_SPREAD},
		    {"mode", [](SoundState& s) { test::set(s, "mode", to_string(SynthMode::RINGMOD)); }, Part::MODE},
		    {"sync", [](SoundState& s) { test::set(s, "osc2/oscillatorSync", "0"); }, Part::OSC_SYNC},
		    {"osc2 type", [](SoundState& s) { test::set(s, "osc2/type", to_string(OscType::SAW)); }, Part::SOURCE_B_TYPE},
		    {"osc1 transpose", [](SoundState& s) { test::set(s, "osc1/transpose", "2"); }, Part::SOURCE_A_TUNING},
		    {"osc2 reversed", [](SoundState& s) { test::set(s, "osc2/reversed", "1"); }, Part::SOURCE_B_PLAYBACK},
		    {"osc2 ranges", [](SoundState& s) {
			    std::vector<deluge::synth::SourceRange> ranges = source_ranges(s, SourceId::B);
			    ranges.push_back({.top_note = 60});
			    s = with_ranges(std::move(s), SourceId::B, std::move(ranges));
		    }, Part::SOURCE_B_RANGES},
		    {"dx7 patch", [](SoundState& s) { s = with_dx7_patch(std::move(s), DxPatchState{.engine_mode = 1}); }, Part::DX7_PATCH},
		    {"retrigger", [](SoundState& s) { test::set(s, "osc1/retrigPhase", "9"); }, Part::RETRIGGER},
		    {"modulator cents", [](SoundState& s) { test::set(s, "modulator1/cents", "1"); }, Part::MODULATOR_TUNING},
		    {"modulator routing", [](SoundState& s) { test::set(s, "modulator2/toModulator1", "0"); }, Part::MODULATOR_ROUTING},
		    {"lfo wave", [](SoundState& s) { test::set(s, "lfo1/type", to_string(LFOType::SQUARE)); }, Part::LFO_WAVE},
		    {"lfo sync", [](SoundState& s) { test::set(s, "lfo4/syncType", number(deluge::plugin::SyncType::TRIPLET)); }, Part::LFO_SYNC},
		    {"effects", [](SoundState& s) { sound_effects(s).saturation.clipping_amount = 4; }, Part::EFFECTS},
		};
		const SoundState before = fm_state();
		for (const Change& change : changes) {
			SoundState after = before;
			change.apply(after);
			Parts expected;
			expected.set(std::to_underlying(change.part));
			expect(DelugeSynth::changes(before, after)).to_equal(expected);
		}
	});

	it("holds the config its state derives and the sound's voices, empty at first", _{
		deluge::instruments::classic::test::FakeHost host;
		DelugeSynth::Instance<deluge::instruments::classic::test::FakeHost> synth{host};
		expect(synth.config().engine.mode).to_equal(SynthMode::SUBTRACTIVE);
		expect(synth.voices().empty()).to_equal(true);
		synth.configure(fm_state());
		expect(synth.config().engine.mode).to_equal(SynthMode::FM);
		expect(int{synth.config().voicing.max_voices}).to_equal(3);
		expect(synth.config().voicing.unison).to_equal(deluge::synth::derive_unison(5, 30, 25));
	});
});
// clang-format on

CPPSPEC_SPEC(deluge_synth)
