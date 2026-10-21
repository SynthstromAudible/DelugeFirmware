#pragma once

/// @file
/// @brief Sound states across what each instrument entry's encoding and reading depend on, for the codec's specs.

#include "engines/deluge/codec.h"

#include <string>
#include <utility>
#include <vector>

namespace deluge::instrument::test {

/// States across what each entry's encoding depends on.
inline std::vector<std::pair<std::string, SoundState>> matrix() {
	std::vector<std::pair<std::string, SoundState>> states;
	states.emplace_back("new", new_synth_state());

	SoundState fm = new_synth_state();
	fm.synth_mode = SynthMode::FM;
	fm.modulator_transpose = {3, -2};
	fm.modulator1_to_modulator0 = true;
	states.emplace_back("fm", fm);

	SoundState ring = new_synth_state();
	ring.synth_mode = SynthMode::RINGMOD;
	ring.sources[1].osc_type = OscType::SAW;
	states.emplace_back("ringmod", ring);

	SoundState sample = new_synth_state();
	sample.sources[0].osc_type = OscType::SAMPLE;
	sample.sources[0].repeat_mode = SampleRepeatMode::ONCE;
	sample.sources[0].ranges = {RangeState{.top_note = 60, .file_path = "SAMPLES/a.wav", .end_pos = 100},
	                            RangeState{.top_note = kOpenTopNote, .file_path = "SAMPLES/b.wav", .end_pos = 200}};
	states.emplace_back("ranged sample", sample);

	SoundState dx = new_synth_state();
	dx.sources[1].osc_type = OscType::DX7;
	dx.sources[1].dx_patch = DxPatchState{.engine_mode = 2, .random_detune = 5};
	dx.sources[1].dx_patch->params[7] = 42;
	states.emplace_back("dx7", dx);

	SoundState wavetable = new_synth_state();
	wavetable.sources[0].osc_type = OscType::WAVETABLE;
	wavetable.sources[0].ranges = {RangeState{.top_note = kOpenTopNote, .file_path = "WAVETABLES/w.wav"}};
	wavetable.oscillator_sync = true;
	states.emplace_back("wavetable", wavetable);

	SoundState synced = new_synth_state();
	synced.lfo_sync_level[1] = deluge::engine::SyncLevel::EIGHTH;
	synced.effects.delay.sync_level = deluge::engine::SyncLevel::QUARTER;
	synced.effects.sidechain.sync_level = deluge::engine::SyncLevel::SIXTEENTH;
	states.emplace_back("synced", synced);

	SoundState nonzero = new_synth_state();
	nonzero.transpose = 5;
	nonzero.cents = -7;
	nonzero.side_chain_send_level = 1234;
	nonzero.effects.saturation.clipping_amount = 4;
	nonzero.effects.stutter.use_song_stutter = false;
	nonzero.effects.filter.lpf_mode = deluge::effects::FilterMode::OFF;
	nonzero.effects.mod_fx.type = deluge::effects::ModFXType::CHORUS;
	nonzero.effects.compressor.ratio = 7 << 24;
	states.emplace_back("non-zero", nonzero);
	return states;
}

} // namespace deluge::instrument::test
