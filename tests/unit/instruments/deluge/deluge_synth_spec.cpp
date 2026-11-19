#include "instruments/deluge/deluge_synth.h"
#include "support/printers/sound_state_printer.h"

#include <cppspec.hpp>
#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::dsp::fixed::FineTune;

namespace {

/// An FM sound with every field the config derives from set away from its default.
FlatSoundState fm_state() {
	FlatSoundState s = FlatSoundState{};
	s.synth_mode = SynthMode::FM;
	s.voice_priority = deluge::plugin::VoicePriority::HIGH;
	s.polyphonic = PolyphonyMode::MONO;
	s.max_voice_count = 3;
	s.transpose = 5;
	s.cents = -17;
	s.num_unison = 5;
	s.unison_detune = 30;
	s.unison_stereo_spread = 25;
	s.oscillator_sync = true;
	s.modulator1_to_modulator0 = true;
	s.modulator_transpose = {7, -5};
	s.modulator_cents = {50, -50};
	s.osc_retrigger_phase = {0, 1U << 30};
	s.modulator_retrigger_phase = {3, 0xFFFFFFFF};
	s.lfo_wave_type[2] = LFOType::SAW;
	s.lfo_sync_level[1] = deluge::plugin::SyncLevel::EIGHTH;
	return s;
}

/// Whether @p a and @p b offset an increment alike.
bool same_tune(const FineTune& a, const FineTune& b) {
	return a.apply(1 << 28) == b.apply(1 << 28);
}

/// One field change and the part it must mark.
struct Change {
	std::string_view name;
	std::function<void(FlatSoundState&)> apply;
	Part part;
};

} // namespace

// clang-format off
describe deluge_synth("the Deluge synth's config and its changed parts", ${
	it("derives every value its voices read from the state", _ {
		const FlatSoundState s = fm_state();
		const deluge::synth::SoundConfig c = DelugeSynth::derive(s);
		expect(c.voicing.priority).to_equal(deluge::plugin::VoicePriority::HIGH);
		expect(c.voicing.polyphony).to_equal(PolyphonyMode::MONO);
		expect(int{c.voicing.max_voices}).to_equal(3);
		expect(int{c.voicing.transpose}).to_equal(5);
		expect(int{c.voicing.cents}).to_equal(-17);
		expect(same_tune(c.voicing.tune, FineTune::of_cents(-17))).to_equal(true);
		expect(c.voicing.unison == deluge::synth::derive_unison(5, 30, 25)).to_equal(true);
		expect(int{c.voicing.unison_detune}).to_equal(30);
		expect(int{c.voicing.unison_spread}).to_equal(25);
		expect(c.engine.mode).to_equal(SynthMode::FM);
		expect(c.engine.osc_sync).to_equal(true);
		expect(c.engine.modulator1_to_modulator0).to_equal(true);
		expect(c.engine.osc_retrigger_phase[1]).to_equal(1U << 30);
		expect(int{c.engine.modulators[0].transpose}).to_equal(7);
		expect(same_tune(c.engine.modulators[1].tune, FineTune::of_cents(-50))).to_equal(true);
		expect(c.engine.modulators[1].retrigger_phase).to_equal(0xFFFFFFFFU);
		expect(c.lfos[2].wave_type).to_equal(LFOType::SAW);
		expect(c.lfos[1].sync_level).to_equal(deluge::plugin::SyncLevel::EIGHTH);
	});

	it("marks nothing when the state is unchanged", _ {
		const FlatSoundState s = fm_state();
		expect(DelugeSynth::changes(s, s).none()).to_equal(true);
	});

	it("marks exactly the part a single field belongs to", _ {
		const std::vector<Change> changes{
		    {"priority", [](FlatSoundState& s) { s.voice_priority = deluge::plugin::VoicePriority::LOW; }, Part::VOICING},
		    {"cents", [](FlatSoundState& s) { s.cents = 3; }, Part::TUNING},
		    {"unison count", [](FlatSoundState& s) { s.num_unison = 2; }, Part::UNISON_COUNT},
		    {"unison detune", [](FlatSoundState& s) { s.unison_detune = 1; }, Part::UNISON_DETUNE},
		    {"unison spread", [](FlatSoundState& s) { s.unison_stereo_spread = 1; }, Part::UNISON_SPREAD},
		    {"mode", [](FlatSoundState& s) { s.synth_mode = SynthMode::RINGMOD; }, Part::MODE},
		    {"sync", [](FlatSoundState& s) { s.oscillator_sync = false; }, Part::OSC_SYNC},
		    {"osc2 type", [](FlatSoundState& s) { s.sources[1].osc_type = OscType::SAW; }, Part::SOURCE_B_TYPE},
		    {"osc1 transpose", [](FlatSoundState& s) { s.sources[0].transpose = 2; }, Part::SOURCE_A_TUNING},
		    {"osc2 reversed", [](FlatSoundState& s) { s.sources[1].reversed = true; }, Part::SOURCE_B_PLAYBACK},
		    {"osc2 ranges", [](FlatSoundState& s) { s.sources[1].ranges.push_back({.top_note = 60}); }, Part::SOURCE_B_RANGES},
		    {"osc1 dx", [](FlatSoundState& s) { s.sources[0].dx_patch.emplace(); }, Part::SOURCE_A_DX},
		    {"retrigger", [](FlatSoundState& s) { s.osc_retrigger_phase[0] = 9; }, Part::RETRIGGER},
		    {"modulator cents", [](FlatSoundState& s) { s.modulator_cents[0] = 1; }, Part::MODULATOR_TUNING},
		    {"modulator routing", [](FlatSoundState& s) { s.modulator1_to_modulator0 = false; }, Part::MODULATOR_ROUTING},
		    {"lfo wave", [](FlatSoundState& s) { s.lfo_wave_type[0] = LFOType::SQUARE; }, Part::LFO_WAVE},
		    {"lfo sync", [](FlatSoundState& s) { s.lfo_sync_type[3] = deluge::plugin::SyncType::TRIPLET; }, Part::LFO_SYNC},
		    {"effects", [](FlatSoundState& s) { s.effects.saturation.clipping_amount = 4; }, Part::EFFECTS},
		    {"sidechain send", [](FlatSoundState& s) { s.side_chain_send_level = 7; }, Part::HOST},
		};
		const FlatSoundState before = fm_state();
		for (const Change& change : changes) {
			FlatSoundState after = before;
			change.apply(after);
			Parts expected;
			expected.set(std::to_underlying(change.part));
			expect(DelugeSynth::changes(before, after) == expected).to_equal(true);
		}
	});
});
// clang-format on

CPPSPEC_SPEC(deluge_synth)
