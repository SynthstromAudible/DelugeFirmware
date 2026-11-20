#pragma once

/// @file
/// @brief Sound states across what each instrument entry's encoding and reading depend on, for the codec's specs.

#include "instruments/deluge/codec.h"
#include "instruments/deluge/state_access.h"
#include "support/record_state.h"

#include <string>
#include <utility>
#include <vector>

namespace deluge::instrument::test {

/// States across what each entry's encoding depends on.
inline std::vector<std::pair<std::string, SoundState>> matrix() {
	namespace record = deluge::test;
	std::vector<std::pair<std::string, SoundState>> states;
	states.emplace_back("new", SoundState{});

	SoundState fm = record::with(SoundState{}, "mode", to_string(SynthMode::FM));
	record::set(fm, "modulator1/transpose", "3");
	record::set(fm, "modulator2/transpose", "-2");
	record::set(fm, "modulator2/toModulator1", "1");
	// A file can hold an FM sound with its filters on, though the engine switch turns them off
	sound_effects(fm).filter = effects::EffectStates{}.filter;
	states.emplace_back("fm", fm);

	SoundState ring = record::with(SoundState{}, "osc2/type", to_string(OscType::SAW));
	record::set(ring, "mode", to_string(SynthMode::RINGMOD));
	states.emplace_back("ringmod", ring);

	SoundState sample = record::with_kind(SoundState{}, SourceId::A, OscType::SAMPLE);
	record::set(sample, "osc1/loopMode", "1"); // ONCE
	sample = with_ranges(std::move(sample), SourceId::A,
	                     {RangeState{.top_note = 60, .file_path = "SAMPLES/a.wav", .end_pos = 100},
	                      RangeState{.top_note = kOpenTopNote, .file_path = "SAMPLES/b.wav", .end_pos = 200}});
	states.emplace_back("ranged sample", sample);

	DxPatchState patch{.engine_mode = 2, .random_detune = 5};
	patch.params[7] = 42;
	const SoundState dx = with_dx_patch(record::with_kind(SoundState{}, SourceId::B, OscType::DX7), SourceId::B, patch);
	states.emplace_back("dx7", dx);

	SoundState wavetable = record::with_kind(SoundState{}, SourceId::A, OscType::WAVETABLE);
	wavetable = with_ranges(std::move(wavetable), SourceId::A,
	                        {RangeState{.top_note = kOpenTopNote, .file_path = "WAVETABLES/w.wav"}});
	record::set(wavetable, "osc2/oscillatorSync", "1");
	states.emplace_back("wavetable", wavetable);

	SoundState synced = record::with(SoundState{}, "lfo2/syncLevel",
	                                 std::to_string(std::to_underlying(deluge::plugin::SyncLevel::EIGHTH)));
	sound_effects(synced).delay.sync_level = deluge::plugin::SyncLevel::QUARTER;
	sound_effects(synced).sidechain.sync_level = deluge::plugin::SyncLevel::SIXTEENTH;
	states.emplace_back("synced", synced);

	SoundState nonzero = SoundState{};
	record::set(nonzero, "transpose", "5");
	record::set(nonzero, "cents", "-7");
	record::set(nonzero, "sideChainSend", "1234");
	effects::EffectStates& fx = sound_effects(nonzero);
	fx.saturation.clipping_amount = 4;
	fx.stutter.use_song_stutter = false;
	fx.filter.lpf_mode = deluge::effects::FilterMode::OFF;
	fx.mod_fx.type = deluge::effects::ModFXType::CHORUS;
	fx.compressor.ratio = 7 << 24;
	states.emplace_back("non-zero", nonzero);
	return states;
}

} // namespace deluge::instrument::test
