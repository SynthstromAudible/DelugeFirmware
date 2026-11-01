// The Deluge instrument's cable acceptance answers as the running sound does: over every source and patched param, in
// every synth mode, with each filter on and off, each global LFO free and synced, on a synth and on a kit's drum,
// `instrument::cable_allowed` over the sound's typed state agrees with `Sound::maySourcePatchToParam` refusing the
// cable or not.

#include "instruments/deluge/cable_acceptance.h"
#include "scenes/menu/bind_host.h"

#include "deluge-project/instrument_slot.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <array>
#include <cstdio>
#include <string>

using namespace deluge::scenes;
namespace params = deluge::modulation::params;

namespace {

/// The number of (source, param) pairs on @p sound at @p slot where the two answers differ; the first is named in
/// @p first.
int32_t disagreements(Sound& sound, ParamManager& manager, deluge::project::SlotAddress slot, bool is_drum,
                      std::string& first) {
	const deluge::instrument::SoundState* state = menu::host_runtime_facts().sound_state(slot);
	if (state == nullptr) {
		first = "no sound state";
		return 1;
	}
	int32_t found = 0;
	for (uint8_t s = 0; s < static_cast<uint8_t>(PatchSource::NONE); ++s) {
		for (int32_t p = 0; p < params::kNumParams; ++p) {
			const bool legacy =
			    sound.maySourcePatchToParam(static_cast<PatchSource>(s), static_cast<uint8_t>(p), &manager)
			    != PatchCableAcceptance::DISALLOWED;
			const bool port =
			    deluge::instrument::cable_allowed(static_cast<deluge::instrument::PatchSource>(s),
			                                      deluge::plugin::ParamId{static_cast<uint32_t>(p)}, *state, is_drum);
			if (legacy != port) {
				if (found++ == 0) {
					char line[96];
					snprintf(line, sizeof line, "source=%d param=%d legacy=%d port=%d", s, static_cast<int>(p), legacy,
					         port);
					first = line;
				}
			}
		}
	}
	return found;
}

/// Sets @p sound into every combination of the settings acceptance reads, and counts disagreements over them all.
int32_t disagreements_everywhere(Sound& sound, Song& song, ParamManager& manager, deluge::project::SlotAddress slot,
                                 bool is_drum, std::string& first) {
	int32_t found = 0;
	for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::FM, SynthMode::RINGMOD}) {
		sound.setSynthMode(mode, &song);
		for (const FilterMode lpf : {FilterMode::TRANSISTOR_24DB, FilterMode::OFF}) {
			sound.setLpfMode(lpf);
			for (const FilterMode hpf : {FilterMode::HPLADDER, FilterMode::OFF}) {
				sound.setHpfMode(hpf);
				for (const SyncLevel lfo1 : {SyncLevel::NONE, SyncLevel::QUARTER}) {
					sound.setLfoSync(LFO1_ID, SyncType::EVEN, lfo1);
					for (const SyncLevel lfo3 : {SyncLevel::NONE, SyncLevel::QUARTER}) {
						sound.setLfoSync(LFO3_ID, SyncType::EVEN, lfo3);
						std::string here;
						const int32_t n = disagreements(sound, manager, slot, is_drum, here);
						if (n != 0 && found == 0) {
							first = here + " mode=" + std::to_string(static_cast<int>(mode))
							        + " lpf=" + std::to_string(static_cast<int>(lpf))
							        + " hpf=" + std::to_string(static_cast<int>(hpf))
							        + " lfo1=" + std::to_string(static_cast<int>(lfo1))
							        + " lfo3=" + std::to_string(static_cast<int>(lfo3));
						}
						found += n;
					}
				}
			}
		}
	}
	return found;
}

} // namespace

// clang-format off
describe cable_acceptance_coherence("a sound's cable acceptance, against the running sound's", ${
	it("agrees on a synth in every state acceptance reads", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		std::string first;
		const int32_t found = disagreements_everywhere(
		    sound, *f.song, f.clip->paramManager,
		    deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, *f.instrument->id()}, false,
		    first);
		expect(first).to_equal(std::string{});
		expect(found).to_equal(0);
	});

	it("agrees on a kit's drum in every state acceptance reads", _ {
		spec::SongFixture f = spec::make_kit_song();
		f.song->setCurrentClip(f.clip);
		auto* drum = spec::add_drum<SoundDrum>(f);
		NoteRow* row = f.clip->getNoteRowForDrum(drum);
		std::string first;
		const int32_t found = disagreements_everywhere(
		    *drum, *f.song, row->paramManager,
		    deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::DRUM, *drum->id()}, true, first);
		expect(first).to_equal(std::string{});
		expect(found).to_equal(0);
	});
});

DELUGE_SPEC(cable_acceptance_coherence_spec, cable_acceptance_coherence)
