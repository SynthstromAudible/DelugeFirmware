#include "instruments/deluge/dx7_init_voice.h"
#include "instruments/deluge/state_rules.h"
#include "support/printers/sound_state_printer.h"

#include <cppspec.hpp>
#include <vector>

using namespace deluge::instrument;
using deluge::effects::FilterMode;

namespace {

/// Three sample ranges, as a sample oscillator plays them.
std::vector<RangeState> three_samples() {
	return {{.top_note = 48, .file_path = "A.WAV", .end_pos = 100},
	        {.top_note = 60, .file_path = "B.WAV", .end_pos = 200},
	        {.top_note = kOpenTopNote, .file_path = "C.WAV", .end_pos = 300}};
}

} // namespace

// clang-format off
describe state_rules("what changes with a sound's engine, oscillator type and loop mode", ${
	it("turns both filters off going into FM", _ {
		SoundState s;
		s.effects.filter.lpf_mode = FilterMode::SVF_BAND;
		const SoundState fm = with_mode(s, SynthMode::FM);
		expect(fm.synth_mode).to_equal(SynthMode::FM);
		expect(fm.effects.filter.lpf_mode).to_equal(FilterMode::OFF);
		expect(fm.effects.filter.hpf_mode).to_equal(FilterMode::OFF);
	});

	it("turns an off filter back on coming out of FM, and leaves one that is on", _ {
		SoundState s;
		s.synth_mode = SynthMode::FM;
		s.effects.filter.lpf_mode = FilterMode::OFF;
		s.effects.filter.hpf_mode = FilterMode::SVF_NOTCH;
		const SoundState sub = with_mode(s, SynthMode::SUBTRACTIVE);
		expect(sub.effects.filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(sub.effects.filter.hpf_mode).to_equal(FilterMode::SVF_NOTCH);
	});

	it("turns both filters back on coming out of FM when both are off", _ {
		SoundState s;
		s.synth_mode = SynthMode::FM;
		s.effects.filter.lpf_mode = FilterMode::OFF;
		s.effects.filter.hpf_mode = FilterMode::OFF;
		const SoundState sub = with_mode(s, SynthMode::RINGMOD);
		expect(sub.effects.filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(sub.effects.filter.hpf_mode).to_equal(FilterMode::HPLADDER);
	});

	it("leaves the filters between two engines that are not FM", _ {
		SoundState s;
		s.effects.filter.lpf_mode = FilterMode::OFF;
		expect(with_mode(s, SynthMode::RINGMOD).effects.filter.lpf_mode).to_equal(FilterMode::OFF);
	});

	it("keeps only the top notes when the ranges change kind", _ {
		SoundState s;
		s.sources[0].osc_type = OscType::SAMPLE;
		s.sources[0].ranges = three_samples();
		const SoundState wt = with_osc_type(s, SourceId::A, OscType::WAVETABLE, three_samples(), false);
		expect(wt.sources[0].osc_type).to_equal(OscType::WAVETABLE);
		expect(wt.sources[0].ranges.size()).to_equal(size_t{3});
		expect(wt.sources[0].ranges[1]).to_equal(RangeState{.top_note = 60});
	});

	it("gives a slot left with no ranges one open range", _ {
		const SoundState s = with_osc_type(SoundState{}, SourceId::B, OscType::SAMPLE, {}, true);
		expect(s.sources[1].ranges.size()).to_equal(size_t{1});
		expect(s.sources[1].ranges[0].top_note).to_equal(kOpenTopNote);
	});

	it("takes the ranges the slot plays when their kind stays", _ {
		const std::vector<RangeState> held = three_samples();
		const SoundState s = with_osc_type(SoundState{}, SourceId::A, OscType::SAMPLE, held, false);
		expect(s.sources[0].ranges == held).to_equal(true);
	});

	it("gives a DX7 slot the init voice, and keeps a patch it has", _ {
		const SoundState fresh = with_osc_type(SoundState{}, SourceId::A, OscType::DX7, {}, false);
		expect(fresh.sources[0].dx_patch.has_value()).to_equal(true);
		expect(fresh.sources[0].dx_patch->params == kDx7InitVoice).to_equal(true);
		SoundState held;
		held.sources[0].dx_patch.emplace();
		held.sources[0].dx_patch->params[0] = 7;
		expect(int{with_osc_type(held, SourceId::A, OscType::DX7, {}, false).sources[0].dx_patch->params[0]})
		    .to_equal(7);
	});

	it("makes pitch and speed independent for stretch, and gives it up leaving stretch", _ {
		const SoundState stretched = with_loop_mode(SoundState{}, SourceId::A, SampleRepeatMode::STRETCH);
		expect(stretched.sources[0].pitch_and_speed_independent).to_equal(true);
		expect(stretched.sources[0].repeat_mode).to_equal(SampleRepeatMode::STRETCH);
		const SoundState looped = with_loop_mode(stretched, SourceId::A, SampleRepeatMode::LOOP);
		expect(looped.sources[0].pitch_and_speed_independent).to_equal(false);
		SoundState independent;
		independent.sources[1].pitch_and_speed_independent = true;
		expect(with_loop_mode(independent, SourceId::B, SampleRepeatMode::ONCE).sources[1].pitch_and_speed_independent)
		    .to_equal(true);
	});

	it("sets a sound up for samples, keeping the rest", _ {
		SoundState s;
		s.unison_detune = 33;
		const SoundState sample = as_sample_sound(s);
		expect(sample.polyphonic).to_equal(PolyphonyMode::AUTO);
		expect(sample.effects.filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(sample.sources[0].osc_type).to_equal(OscType::SAMPLE);
		expect(sample.sources[1].osc_type).to_equal(OscType::SAMPLE);
		expect(int{sample.unison_detune}).to_equal(33);
	});

	it("sets up the default synth: a saw an octave down over a square, four detuned parts", _ {
		const SoundState synth = as_default_synth(SoundState{});
		expect(synth.sources[0].osc_type).to_equal(OscType::SAW);
		expect(int{synth.sources[1].transpose}).to_equal(-12);
		expect(int{synth.transpose}).to_equal(-12);
		expect(int{synth.num_unison}).to_equal(4);
		expect(int{synth.unison_detune}).to_equal(10);
		expect(synth.effects.filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
	});

	it("sets up a blank DX7 synth on the init voice, and leaves a blank synth's oscillators", _ {
		const SoundState dx = as_blank_synth(SoundState{}, true);
		expect(dx.sources[0].osc_type).to_equal(OscType::DX7);
		expect(dx.sources[0].dx_patch.has_value()).to_equal(true);
		expect(dx.sources[0].dx_patch->params == kDx7InitVoice).to_equal(true);
		expect(as_blank_synth(SoundState{}, false) == SoundState{}).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(state_rules)
