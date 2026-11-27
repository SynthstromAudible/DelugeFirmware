#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "support/printers/sound_record_printer.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <utility>
#include <vector>

using namespace deluge::instruments::classic;
using deluge::effects::FilterMode;

namespace {

/// Three sample ranges, as a sample oscillator plays them.
std::vector<deluge::synth::SourceRange> three_samples() {
	return {{.top_note = 48, .file_path = "A.WAV", .zone = {.end = 100}},
	        {.top_note = 60, .file_path = "B.WAV", .zone = {.end = 200}},
	        {.top_note = kOpenTopNote, .file_path = "C.WAV", .zone = {.end = 300}}};
}

} // namespace

// clang-format off
describe state_rules("what changes with a sound's engine, oscillator type and loop mode", ${
	it("turns both filters off going into FM", _ {
		SoundState s;
		sound_effects(s).filter.lpf_mode = FilterMode::SVF_BAND;
		const SoundState fm = switch_engine(s, SynthMode::FM);
		expect(synth_mode(fm)).to_equal(SynthMode::FM);
		expect(sound_effects(fm).filter.lpf_mode).to_equal(FilterMode::OFF);
		expect(sound_effects(fm).filter.hpf_mode).to_equal(FilterMode::OFF);
	});

	it("turns an off filter back on coming out of FM, and leaves one that is on", _ {
		SoundState s = switch_engine(SoundState{}, SynthMode::FM);
		sound_effects(s).filter.lpf_mode = FilterMode::OFF;
		sound_effects(s).filter.hpf_mode = FilterMode::SVF_NOTCH;
		const SoundState sub = switch_engine(s, SynthMode::SUBTRACTIVE);
		expect(sound_effects(sub).filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(sound_effects(sub).filter.hpf_mode).to_equal(FilterMode::SVF_NOTCH);
	});

	it("turns both filters back on coming out of FM when both are off", _ {
		SoundState s = switch_engine(SoundState{}, SynthMode::FM);
		sound_effects(s).filter.lpf_mode = FilterMode::OFF;
		sound_effects(s).filter.hpf_mode = FilterMode::OFF;
		const SoundState sub = switch_engine(s, SynthMode::RINGMOD);
		expect(sound_effects(sub).filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(sound_effects(sub).filter.hpf_mode).to_equal(FilterMode::HPLADDER);
	});

	it("leaves the filters between two engines that are not FM", _ {
		SoundState s;
		sound_effects(s).filter.lpf_mode = FilterMode::OFF;
		expect(sound_effects(switch_engine(s, SynthMode::RINGMOD)).filter.lpf_mode).to_equal(FilterMode::OFF);
	});

	it("keeps only the top notes when the ranges change kind", _ {
		SoundState s = deluge::test::with_kind(SoundState{}, SourceId::A, OscType::SAMPLE);
		s = with_ranges(std::move(s), SourceId::A, three_samples());
		const SoundState wt = switch_source(s, SourceId::A, OscType::WAVETABLE, three_samples(), false);
		const std::vector<deluge::synth::SourceRange> ranges = source_ranges(wt, SourceId::A);
		expect(source_type(wt, SourceId::A)).to_equal(OscType::WAVETABLE);
		expect(ranges.size()).to_equal(size_t{3});
		expect(ranges[1]).to_equal(deluge::synth::SourceRange{.top_note = 60});
	});

	it("gives a slot left with no ranges one open range", _ {
		const SoundState s = switch_source(SoundState{}, SourceId::B, OscType::SAMPLE, {}, true);
		const std::vector<deluge::synth::SourceRange> ranges = source_ranges(s, SourceId::B);
		expect(ranges.size()).to_equal(size_t{1});
		expect(ranges[0].top_note).to_equal(kOpenTopNote);
	});

	it("takes the ranges the slot plays when their kind stays", _ {
		const std::vector<deluge::synth::SourceRange> held = three_samples();
		const SoundState s = switch_source(SoundState{}, SourceId::A, OscType::SAMPLE, held, false);
		expect(source_ranges(s, SourceId::A)).to_equal(held);
	});

	it("makes pitch and speed independent for stretch, and gives it up leaving stretch", _ {
		const SoundState stretched = with_loop_mode(SoundState{}, SourceId::A, SampleRepeatMode::STRETCH);
		expect(sample_playback(stretched, SourceId::A).pitch_and_speed_independent).to_equal(true);
		expect(sample_playback(stretched, SourceId::A).repeat_mode).to_equal(SampleRepeatMode::STRETCH);
		const SoundState looped = with_loop_mode(stretched, SourceId::A, SampleRepeatMode::LOOP);
		expect(sample_playback(looped, SourceId::A).pitch_and_speed_independent).to_equal(false);
		const SoundState independent = deluge::test::with(SoundState{}, "osc2/timeStretchEnable", "1");
		expect(sample_playback(with_loop_mode(independent, SourceId::B, SampleRepeatMode::ONCE), SourceId::B)
		           .pitch_and_speed_independent)
		    .to_equal(true);
	});

	it("takes the effect settings that follow the song from the song's defaults, and keeps the rest", _ {
		const deluge::effects::SongDefaults song{.delay_sync_level = deluge::plugin::SyncLevel::EIGHTH,
		                                          .sidechain = {.attack = 3, .release = 4,
		                                                        .sync_type = deluge::plugin::SyncType::EVEN,
		                                                        .sync_level = deluge::plugin::SyncLevel::QUARTER}};
		SoundState s;
		sound_effects(s).delay.ping_pong = false;
		const SoundState fresh = with_song_defaults(s, song);
		expect(sound_effects(fresh).delay.sync_level).to_equal(deluge::plugin::SyncLevel::EIGHTH);
		expect(sound_effects(fresh).sidechain.attack).to_equal(3);
		expect(sound_effects(fresh).sidechain.release).to_equal(4);
		expect(sound_effects(fresh).sidechain.sync_level).to_equal(deluge::plugin::SyncLevel::QUARTER);
		expect(sound_effects(fresh).delay.ping_pong).to_be_false();
	});

	it("sets a sound up for samples, keeping the rest", _ {
		const SoundState s = deluge::test::with(SoundState{}, "unison/detune", "33");
		const SoundState sample = as_sample_sound(s);
		expect(voicing(sample).polyphony).to_equal(PolyphonyMode::AUTO);
		expect(sound_effects(sample).filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(source_type(sample, SourceId::A)).to_equal(OscType::SAMPLE);
		expect(source_type(sample, SourceId::B)).to_equal(OscType::SAMPLE);
		expect(int{unison(sample).detune}).to_equal(33);
	});

	it("sets up the default synth: a saw an octave down over a square, four detuned parts", _ {
		const SoundState synth = as_default_synth(SoundState{});
		expect(source_type(synth, SourceId::A)).to_equal(OscType::SAW);
		expect(int{source_tuning(synth, SourceId::B).transpose}).to_equal(-12);
		expect(int{voicing(synth).master.transpose}).to_equal(-12);
		expect(int{unison(synth).count}).to_equal(4);
		expect(int{unison(synth).detune}).to_equal(10);
		expect(sound_effects(synth).filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
	});

	it("sets up a blank DX7 synth in DX7 mode on the init voice, and leaves a blank synth's oscillators", _ {
		const SoundState dx = as_blank_synth(SoundState{}, true);
		expect(synth_mode(dx)).to_equal(SynthMode::DX7);
		const DxPatchState* patch = dx7_patch(dx);
		expect(patch).not_().to_be_null();
		expect(patch->params).to_equal(kDx7InitVoice);
		DxPatchState kept;
		kept.params[0] = 7;
		const SoundState held = as_blank_synth(with_dx7_patch(SoundState{}, kept), true);
		expect(int{dx7_patch(held)->params[0]}).to_equal(7);
		expect(as_blank_synth(SoundState{}, false)).to_equal(SoundState{});
	});
});
// clang-format on

CPPSPEC_SPEC(state_rules)
