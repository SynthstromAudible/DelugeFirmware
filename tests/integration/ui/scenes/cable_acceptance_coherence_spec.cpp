// The Deluge instrument's cable acceptance answers as the running sound does: over every source and patched param, in
// every synth mode, with each filter on and off, each global LFO free and synced, on a synth and on a kit's drum,
// `instruments::classic::cable_allowed` over the sound's typed state agrees with `Sound::maySourcePatchToParam`
// refusing the cable or not.

#include "gui/ui/shell_host/host_ports.h"
#include "instruments/classic/cable_acceptance.h"

#include "instruments/classic/settings.h"
#include "instruments/classic/words.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/set_engine_setting.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/instrument_slot.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include "support/spec_main.h"

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace deluge::ui::scenes;
namespace params = deluge::modulation::params;

namespace {

/// The number of (source, param) pairs on @p sound at @p slot where the two answers differ; the first is named in
/// @p first.
int32_t disagreements(Sound& sound, ParamManager& manager, deluge::project::SlotAddress slot, bool is_drum,
                      std::string& first) {
	const deluge::instruments::classic::SoundState* state =
	    deluge::gui::shell_host::host_ports().sound->sound_state(slot);
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
			const bool port = deluge::instruments::classic::cable_allowed(
			    static_cast<deluge::instruments::classic::PatchSource>(s),
			    deluge::instruments::classic::flat_param_id(static_cast<uint32_t>(p)), *state, is_drum);
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

/// Takes @p sound's setting at @p path to the file text @p text, through its slot's Edit.
/// @return Whether the Edit applied.
bool set_setting(Sound& sound, Song& song, deluge::project::SlotAddress slot, std::string_view path,
                 std::string_view text) {
	deluge::instruments::classic::SoundState wanted = sound.state();
	const std::optional<deluge::serialization::Value> written = deluge::instruments::classic::canonical_setting(
	    path, deluge::serialization::Value::makeScalar(std::string(text)));
	if (!written || !deluge::instruments::classic::allows_setting(wanted, path, *written)) {
		return false;
	}
	(void)deluge::instruments::classic::write_setting(wanted, path, *written);
	const std::optional<deluge::serialization::Value> value = deluge::instruments::classic::setting_value(wanted, path);
	if (!value) {
		return false;
	}
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetEngineSetting edit{slot, std::string(path), *value};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

/// Syncs @p sound's LFO @p lfo (1–4) evenly to @p level, through its slot's Edits.
void set_lfo_sync(Sound& sound, Song& song, deluge::project::SlotAddress slot, int32_t lfo, SyncLevel level) {
	const std::string prefix = "lfo" + std::to_string(lfo) + "/";
	set_setting(sound, song, slot, prefix + "syncType", std::to_string(std::to_underlying(SyncType::EVEN)));
	set_setting(sound, song, slot, prefix + "syncLevel", std::to_string(std::to_underlying(level)));
}

/// Sets @p sound into every combination of the settings acceptance reads, and counts disagreements over them all.
int32_t disagreements_everywhere(Sound& sound, Song& song, ParamManager& manager, deluge::project::SlotAddress slot,
                                 bool is_drum, std::string& first) {
	int32_t found = 0;
	// Ring mod takes ring-moddable oscillators; acceptance reads no oscillator type, so both are saws throughout.
	for (const char* path : {"osc1/type", "osc2/type"}) {
		if (!set_setting(sound, song, slot, path, deluge::instruments::classic::to_string(OscType::SAW))) {
			first = std::string(path) + " refused";
			return 1;
		}
	}
	for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::FM, SynthMode::RINGMOD, SynthMode::DX7}) {
		if (!set_setting(sound, song, slot, "mode", deluge::instruments::classic::to_string(mode))) {
			first = "mode " + std::to_string(static_cast<int>(mode)) + " refused";
			return found + 1;
		}
		for (const FilterMode lpf : {FilterMode::TRANSISTOR_24DB, FilterMode::OFF}) {
			deluge::model::changeEffects(*sound.effectsOwner(),
			                             [&](deluge::effects::EffectStates& e) { e.filter.lpf_mode = lpf; });
			for (const FilterMode hpf : {FilterMode::HPLADDER, FilterMode::OFF}) {
				deluge::model::changeEffects(*sound.effectsOwner(),
				                             [&](deluge::effects::EffectStates& e) { e.filter.hpf_mode = hpf; });
				for (const SyncLevel lfo1 : {SyncLevel::NONE, SyncLevel::QUARTER}) {
					set_lfo_sync(sound, song, slot, 1, lfo1);
					for (const SyncLevel lfo3 : {SyncLevel::NONE, SyncLevel::QUARTER}) {
						set_lfo_sync(sound, song, slot, 3, lfo3);
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

/// Whether @p manager's cables hold the one from @p source to the patched param at runtime index @p p.
bool holds_cable(ParamManager& manager, PatchSource source, uint8_t p) {
	PatchCableSet* cables = manager.getPatchCableSet();
	for (int32_t c = 0; c < cables->numPatchCables; ++c) {
		if (cables->patchCables[c].from == source
		    && cables->patchCables[c].destination.word == deluge::synth::CableTarget::param(p).word) {
			return true;
		}
	}
	return false;
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

	it("keeps a parked engine's params and cables across a mode switch, reachable again on the way back", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *f.instrument->id()};
		ParamManager& manager = f.clip->paramManager;
		PatchedParamSet* patched = manager.getPatchedParamSet();
		const auto mode = [&](SynthMode m) {
			return set_setting(sound, *f.song, slot, "mode", deluge::instruments::classic::to_string(m));
		};
		const auto set_value = [&](int32_t p, int32_t value) {
			patched->params[p].setBase(*patched->docForParam(p), value);
		};
		expect(mode(SynthMode::SUBTRACTIVE)).to_equal(true);
		set_value(params::LOCAL_OSC_A_VOLUME, 0x12345678);
		manager.getPatchCableSet()->appendCable({PatchSource::LFO_GLOBAL_1,
		                                         deluge::synth::CableTarget::param(params::LOCAL_OSC_A_VOLUME),
		                                         deluge::synth::CablePolarity::BIPOLAR});
		expect(mode(SynthMode::DX7)).to_equal(true);
		set_value(params::LOCAL_DX7_LEVEL, 0x23456789);
		manager.getPatchCableSet()->appendCable({PatchSource::LFO_GLOBAL_2,
		                                         deluge::synth::CableTarget::param(params::LOCAL_DX7_LEVEL),
		                                         deluge::synth::CablePolarity::BIPOLAR});
		// Parked: kept, but no cable reaches it
		expect(patched->params[params::LOCAL_OSC_A_VOLUME].played()).to_equal(0x12345678);
		expect(holds_cable(manager, PatchSource::LFO_GLOBAL_1, params::LOCAL_OSC_A_VOLUME)).to_equal(true);
		expect(sound.maySourcePatchToParam(PatchSource::LFO_GLOBAL_1, params::LOCAL_OSC_A_VOLUME, &manager)
		       == PatchCableAcceptance::DISALLOWED)
		    .to_equal(true);
		expect(mode(SynthMode::SUBTRACTIVE)).to_equal(true);
		expect(patched->params[params::LOCAL_DX7_LEVEL].played()).to_equal(0x23456789);
		expect(holds_cable(manager, PatchSource::LFO_GLOBAL_2, params::LOCAL_DX7_LEVEL)).to_equal(true);
		expect(sound.maySourcePatchToParam(PatchSource::LFO_GLOBAL_2, params::LOCAL_DX7_LEVEL, &manager)
		       == PatchCableAcceptance::DISALLOWED)
		    .to_equal(true);
		expect(sound.maySourcePatchToParam(PatchSource::LFO_GLOBAL_1, params::LOCAL_OSC_A_VOLUME, &manager)
		       == PatchCableAcceptance::DISALLOWED)
		    .to_equal(false);
		expect(mode(SynthMode::DX7)).to_equal(true);
		expect(patched->params[params::LOCAL_OSC_A_VOLUME].played()).to_equal(0x12345678);
		expect(patched->params[params::LOCAL_DX7_LEVEL].played()).to_equal(0x23456789);
		expect(sound.maySourcePatchToParam(PatchSource::LFO_GLOBAL_2, params::LOCAL_DX7_LEVEL, &manager)
		       == PatchCableAcceptance::DISALLOWED)
		    .to_equal(false);
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
