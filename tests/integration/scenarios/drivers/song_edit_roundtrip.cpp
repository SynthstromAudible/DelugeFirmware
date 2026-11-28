// Edit-aware round-trip driver (DOM-live-authoritative automation): load a song, apply a DOM-first
// edit to a song-global unpatched param, save, reload, and assert the edit persisted through the DOM path.
//
//   song_edit_roundtrip <song.XML>
//
// This exercises the whole DOM-authoritative edit loop off-hardware — the piece the golden load->save harness
// structurally cannot see (it never edits). We:
//   1. load(original)                                             -> song + populated DOM store
//   2. setUnpatchedValueDomFirst(store, p, SENTINEL, engineParam) -> write DOM store lane + reproject engine
//   3. save                                                       -> gen1
//   4. load(gen1)                                                 -> reload
//   5. require reloaded store[p] == SENTINEL AND reloaded engine[p] == SENTINEL
//
// It holds whether save reads the engine (the DOM-first edit reprojects into it) or the DOM store.
//
// Exit: 0 = edit persisted; 1 = load error; 2 = usage/IO; 3 = edit not applied / not persisted.

#include "instruments/classic/settings.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "instruments/classic/words.h"
#include "io/midi/midi_device_manager.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/setting_edit.h"
#include "model/fx/chain_params.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/codec.h"
#include "project/param_automation.h"
#include "project/param_edit.h"
#include "storage/storage_manager.h"
#include "support/record_state.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "model/mod_controllable/filters/filter_config.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/patch_cable_words.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/param_stores.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/value_tree_deserializer.h"
#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

using namespace deluge;

namespace {

using deluge::scenarios::driver::patchedStore;
using deluge::scenarios::driver::unpatchedStore;

using deluge::scenarios::driver::Io;

/// Text edits on a saved song, in the syntax of the format songs are saved in: an XML attribute `name="value"` or a
/// JSON member `"name": value`.
class SavedSong {
public:
	explicit SavedSong(const char* text, long length) : text_(text, length) {}

	[[nodiscard]] const std::string& text() const { return text_; }

	/// The attribute or member @p name with @p value, as this format writes it.
	[[nodiscard]] static std::string member(const std::string& name, const std::string& value) {
		return json() ? "\"" + name + "\": " + value : name + "=\"" + value + "\"";
	}

	/// Whether the song holds the attribute or member @p name.
	[[nodiscard]] bool holds(const std::string& name) const { return find(name).first != std::string::npos; }

	/// Takes the attribute or member @p name out; false when the song holds none.
	bool erase(const std::string& name) {
		auto [at, end] = find(name);
		if (at == std::string::npos) {
			return false;
		}
		if (json()) {
			if (end < text_.size() && text_[end] == ',') {
				end++;
			}
			else {
				at = text_.rfind(',', at);
			}
		}
		text_.erase(at, end - at);
		return true;
	}

	/// Gives the attribute or member @p name the value @p value; false when the song holds none.
	bool replace(const std::string& name, const std::string& value) {
		const auto [at, end] = find(name);
		if (at == std::string::npos) {
			return false;
		}
		text_.replace(at, end - at, member(name, value));
		return true;
	}

	/// Replaces the first attribute or member written as @p from (a whole one: `"id": 1` is not the start of
	/// `"id": 15`) with @p to; false when the song holds none.
	bool replaceMember(const std::string& from, const std::string& to) {
		for (size_t at = text_.find(from); at != std::string::npos; at = text_.find(from, at + 1)) {
			const size_t end = at + from.size();
			if (end < text_.size() && std::isalnum(static_cast<unsigned char>(text_[end]))) {
				continue;
			}
			text_.replace(at, from.size(), to);
			return true;
		}
		return false;
	}

	/// Moves the attribute or member @p name to the front of the song's own; false when the song holds none.
	bool moveToFront(const std::string& name) {
		const auto [at, end] = find(name);
		const size_t songAt = text_.find(json() ? "\"song\": {" : "<song");
		if (at == std::string::npos || songAt == std::string::npos) {
			return false;
		}
		const std::string moved = text_.substr(at, end - at);
		erase(name);
		text_.insert(songAt + (json() ? std::strlen("\"song\": {") : std::strlen("<song")),
		             json() ? moved + "," : " " + moved);
		return true;
	}

	/// Takes away every output's own id (the id each output names first), as a song from before outputs had ids.
	bool dropOutputIds() {
		const size_t open = text_.find(json() ? "\"instruments\": [" : "<instruments>");
		const size_t close = json() ? closingBracketAfter(open) : text_.find("</instruments>");
		if (open == std::string::npos || close == std::string::npos) {
			return false;
		}
		const std::regex firstId = json() ? std::regex(R"re((\{"[A-Za-z]+": \{\s*)"id": [0-9]+,\s*)re")
		                                  : std::regex("(<[A-Za-z]+\\s+)id=\"[0-9]+\"\\s*");
		text_.replace(open, close - open, std::regex_replace(text_.substr(open, close - open), firstId, "$1"));
		return true;
	}

private:
	[[nodiscard]] static bool json() {
		return deluge::storage::kSongSaveFormat == deluge::storage::DocumentFormat::JSON;
	}

	/// Where the attribute or member @p name starts and ends, or npos.
	[[nodiscard]] std::pair<size_t, size_t> find(const std::string& name) const {
		const std::string key = json() ? "\"" + name + "\": " : name + "=\"";
		const size_t at = text_.find(key);
		if (at == std::string::npos) {
			return {std::string::npos, std::string::npos};
		}
		const size_t end = json() ? text_.find_first_of(",\n}", at + key.size()) : text_.find('"', at + key.size()) + 1;
		return {at, end};
	}

	/// The position of the `]` closing the JSON array whose name starts at @p from, or npos.
	[[nodiscard]] size_t closingBracketAfter(size_t from) const {
		if (from == std::string::npos) {
			return std::string::npos;
		}
		int32_t depth = 0;
		bool inString = false;
		for (size_t i = text_.find('[', from); i < text_.size(); i++) {
			const char c = text_[i];
			if (inString) {
				if (c == '\\') {
					i++;
				}
				else if (c == '"') {
					inString = false;
				}
			}
			else if (c == '"') {
				inString = true;
			}
			else if (c == '[') {
				depth++;
			}
			else if (c == ']' && --depth == 0) {
				return i;
			}
		}
		return std::string::npos;
	}

	std::string text_;
};

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

namespace params = deluge::modulation::params;

/// A tuner set up for @p cents the way Sound does (a cent is 42949672 in the tuner's detune scale).
deluge::dsp::fixed::FineTune centsTuner(int32_t cents) {
	return deluge::dsp::fixed::FineTune::of_step(cents * 42949672);
}

/// Whether two tuners detune a phase increment identically.
bool sameTuning(const deluge::dsp::fixed::FineTune& a, const deluge::dsp::fixed::FineTune& b) {
	constexpr int32_t kProbe = 1 << 24;
	return a.apply(kProbe) == b.apply(kProbe);
}

/// The oscillator slots a sound has, in file order.
constexpr std::array kSourceIds{instruments::classic::SourceId::A, instruments::classic::SourceId::B};

/// Each bound Sound of @p song: a synth instrument's, or a kit's sound drum's, live or hibernating.
template <typename F>
void forEachBoundSound(Song& song, F&& f) {
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* o = first; o != nullptr; o = o->next) {
			if (o->type == OutputType::SYNTH) {
				Sound* sound = static_cast<SoundInstrument*>(o);
				f(*sound);
			}
			else if (o->type == OutputType::KIT) {
				for (Drum* d = static_cast<Kit*>(o)->firstDrum; d != nullptr; d = d->next) {
					if (d->type == DrumType::SOUND) {
						Sound* sound = static_cast<SoundDrum*>(d);
						f(*sound);
					}
				}
			}
		}
	}
}

/// A retrigger phase word as a file writes it: the signed 32-bit number with the same bits.
std::string phaseText(uint32_t word) {
	return std::to_string(std::bit_cast<int32_t>(word));
}

/// Takes @p sound's setting at @p path to @p text (as the file writes it) as an editor does (an Edit on its slot in
/// the current song, else the unslotted write): the value is what the setting reads in the record with @p text written
/// to a copy. @return Whether the state then holds the value.
bool setThrough(Sound& sound, std::string_view path, std::string_view text) {
	const std::optional<deluge::instruments::classic::SoundState> wanted =
	    deluge::test::try_set(*sound.state_, path, text);
	if (!wanted) {
		return false;
	}
	const std::optional<deluge::serialization::Value> value =
	    deluge::instruments::classic::setting_value(*wanted, path);
	if (!value) {
		return false;
	}
	deluge::edits::settingFor(sound, path, *value);
	const std::optional<deluge::serialization::Value> landed =
	    deluge::instruments::classic::setting_value(*sound.state_, path);
	return landed && landed->str() == value->str();
}

/// Every bound Sound's snapshot must equal the projection of its record, and each setting's Edit must
/// write the record and bring the snapshot along. @return the number of mismatches, each printed under @p label.
int32_t checkSoundConfigs(Song& song, const char* label) {
	int32_t sounds = 0, bad = 0;
	Sound* probe = nullptr;
	Sound* modeProbe = nullptr; // one that can go to ring mod and back (or back from it) without leaving FM
	Sound* fmProbe = nullptr;   // one whose modulators an Edit writes
	forEachBoundSound(song, [&](Sound& sound) {
		const auto d = [&sound]() -> const instruments::classic::SoundState& { return *sound.state_; }; // read live
		sounds++;
		probe = (probe != nullptr) ? probe : &sound;
		const bool ringable = std::ranges::all_of(kSourceIds, [&](instruments::classic::SourceId id) {
			return instruments::classic::source_type(d(), id) <= OscType::WAVETABLE;
		});
		if (modeProbe == nullptr && instruments::classic::synth_mode(d()) != SynthMode::FM && ringable) {
			modeProbe = &sound;
		}
		if (fmProbe == nullptr && instruments::classic::synth_mode(d()) == SynthMode::FM) {
			fmProbe = &sound;
		}
		if (sound.getTranspose() != instruments::classic::voicing(d()).master.transpose) {
			std::fprintf(io.err, "M4b FAIL %s: transpose snapshot=%d record=%d\n", label, sound.getTranspose(),
			             instruments::classic::voicing(d()).master.transpose);
			bad++;
		}
		// A copy: the unison-table check below reprojects, and every field must be compared as loaded.
		const deluge::synth::SoundConfig snap = sound.config();
		auto expect = [&](bool ok, const char* field) {
			if (!ok) {
				std::fprintf(io.err, "M4b FAIL %s: %s snapshot != record\n", label, field);
				bad++;
			}
		};
		expect(snap.engine.osc_sync == instruments::classic::oscillator_sync(d()), "oscillator_sync");
		expect(snap.voicing.priority == static_cast<VoicePriority>(instruments::classic::voicing(d()).priority),
		       "voice_priority");
		expect(snap.engine.modulator1_to_modulator0 == instruments::classic::modulator1_to_modulator0(d()),
		       "modulator1_to_modulator0");
		expect(sound.renderEffects() == sound.effects(), "effects");
		expect(snap.voicing.unison.count == instruments::classic::unison(d()).count, "num_unison");
		expect(snap.voicing.unison_detune == instruments::classic::unison(d()).detune, "unison_detune");
		expect(snap.voicing.unison_spread == instruments::classic::unison(d()).spread, "unison_stereo_spread");
		{
			// The unison tables are derived; the loaded ones must match a fresh projection from the record.
			sound.deriveConfig();
			for (int32_t u = 0; u < snap.voicing.unison.count && snap.voicing.unison.count != 1; u++) {
				expect(sameTuning(snap.voicing.unison.detunes[u], sound.config().voicing.unison.detunes[u]),
				       "unison_detuners");
				expect(snap.voicing.unison.pans[u] == sound.config().voicing.unison.pans[u], "unison_pan");
			}
		}
		expect(snap.voicing.cents == instruments::classic::voicing(d()).master.cents, "cents");
		expect(sameTuning(snap.voicing.tune, centsTuner(instruments::classic::voicing(d()).master.cents)),
		       "fine_tuner");
		for (int32_t l = 0; l < LFO_COUNT; l++) {
			expect(snap.lfos[l].shape == static_cast<LFOType>(instruments::classic::lfo(d(), l).shape),
			       "lfo_state.shape");
			expect(snap.lfos[l].sync_type == static_cast<SyncType>(instruments::classic::lfo(d(), l).sync_type),
			       "lfo_state.sync_type");
			expect(snap.lfos[l].sync_level == static_cast<SyncLevel>(instruments::classic::lfo(d(), l).sync_level),
			       "lfo_state.sync_level");
		}
		for (int32_t i = 0; i < 2; i++) {
			expect(snap.engine.modulators[i].cents == instruments::classic::modulators(d())[i].tuning.cents,
			       "modulator_cents");
			expect(sameTuning(snap.engine.modulators[i].tune,
			                  centsTuner(instruments::classic::modulators(d())[i].tuning.cents)),
			       "modulator_transposers");
			expect(snap.engine.modulators[i].transpose == instruments::classic::modulators(d())[i].tuning.transpose,
			       "modulator_transpose");
			expect(snap.engine.osc_retrigger_phase[i]
			           == instruments::classic::phase_word(instruments::classic::source_retrigger(d(), kSourceIds[i])),
			       "osc_retrigger_phase");
			expect(snap.engine.modulators[i].retrigger_phase
			           == instruments::classic::phase_word(instruments::classic::modulators(d())[i].retrigger),
			       "modulator_retrigger_phase");
		}
		if (sound.getSynthMode() != static_cast<SynthMode>(instruments::classic::synth_mode(d()))) {
			std::fprintf(io.err, "M4b FAIL %s: synthMode snapshot=%d record=%d\n", label,
			             static_cast<int>(sound.getSynthMode()), instruments::classic::synth_mode(d()));
			bad++;
		}
	});
	if (probe != nullptr) {
		Sound& sound = *probe;
		const auto d = [&sound]() -> const instruments::classic::SoundState& { return *sound.state_; }; // read live
		auto roundTrip = [&](bool ok, const char* setting) {
			if (!ok) {
				std::fprintf(io.err, "M4b FAIL %s: the %s Edit did not write the record and reproject\n", label,
				             setting);
				bad++;
			}
		};
		const int16_t transpose = sound.getTranspose();
		const int16_t otherTranspose = static_cast<int16_t>(transpose + 7);
		roundTrip(setThrough(sound, "transpose", std::to_string(otherTranspose))
		              && sound.getTranspose() == otherTranspose
		              && instruments::classic::voicing(d()).master.transpose == otherTranspose,
		          "transpose");
		setThrough(sound, "transpose", std::to_string(transpose));
		const deluge::synth::SoundConfig before = sound.config();
		roundTrip(setThrough(sound, "osc2/oscillatorSync", before.engine.osc_sync ? "0" : "1")
		              && sound.config().engine.osc_sync == instruments::classic::oscillator_sync(d())
		              && instruments::classic::oscillator_sync(d()) != before.engine.osc_sync,
		          "osc2/oscillatorSync");
		setThrough(sound, "osc2/oscillatorSync", before.engine.osc_sync ? "1" : "0");
		const VoicePriority otherPriority =
		    before.voicing.priority == VoicePriority::HIGH ? VoicePriority::LOW : VoicePriority::HIGH;
		roundTrip(setThrough(sound, "voicePriority", std::to_string(std::to_underlying(otherPriority)))
		              && sound.config().voicing.priority == otherPriority
		              && static_cast<VoicePriority>(instruments::classic::voicing(d()).priority) == otherPriority,
		          "voicePriority");
		setThrough(sound, "voicePriority", std::to_string(std::to_underlying(before.voicing.priority)));
		const uint32_t otherPhase = before.engine.osc_retrigger_phase[1] + 5;
		roundTrip(setThrough(sound, "osc2/retrigPhase", phaseText(otherPhase))
		              && sound.config().engine.osc_retrigger_phase[1]
		                     == instruments::classic::phase_word(
		                         instruments::classic::source_retrigger(d(), instruments::classic::SourceId::B))
		              && instruments::classic::phase_word(
		                     instruments::classic::source_retrigger(d(), instruments::classic::SourceId::B))
		                     == otherPhase,
		          "osc2/retrigPhase");
		setThrough(sound, "osc2/retrigPhase", phaseText(before.engine.osc_retrigger_phase[1]));
		const deluge::synth::LfoState lfo = before.lfos[LFO2_ID];
		const LFOType otherWave = lfo.shape == LFOType::SQUARE ? LFOType::SAW : LFOType::SQUARE;
		roundTrip(setThrough(sound, "lfo2/type", instruments::classic::to_string(otherWave))
		              && sound.config().lfos[LFO2_ID].shape == otherWave
		              && static_cast<LFOType>(instruments::classic::lfo(d(), LFO2_ID).shape) == otherWave,
		          "lfo2/type");
		setThrough(sound, "lfo2/type", instruments::classic::to_string(lfo.shape));
		const SyncLevel otherLevel = lfo.sync_level == SYNC_LEVEL_8TH ? SYNC_LEVEL_16TH : SYNC_LEVEL_8TH;
		const bool typeLanded =
		    setThrough(sound, "lfo2/syncType", std::to_string(std::to_underlying(SYNC_TYPE_TRIPLET)));
		roundTrip(typeLanded && setThrough(sound, "lfo2/syncLevel", std::to_string(plugin::index_of(otherLevel)))
		              && sound.config().lfos[LFO2_ID].sync_type == SYNC_TYPE_TRIPLET
		              && sound.config().lfos[LFO2_ID].sync_level == otherLevel
		              && static_cast<SyncType>(instruments::classic::lfo(d(), LFO2_ID).sync_type) == SYNC_TYPE_TRIPLET
		              && static_cast<SyncLevel>(instruments::classic::lfo(d(), LFO2_ID).sync_level) == otherLevel,
		          "lfo2/syncType and lfo2/syncLevel");
		setThrough(sound, "lfo2/syncLevel", std::to_string(plugin::index_of(lfo.sync_level)));
		setThrough(sound, "lfo2/syncType", std::to_string(std::to_underlying(lfo.sync_type)));
		const int8_t otherCents = static_cast<int8_t>(before.voicing.cents == 25 ? -25 : 25);
		roundTrip(setThrough(sound, "cents", std::to_string(otherCents)) && sound.config().voicing.cents == otherCents
		              && instruments::classic::voicing(d()).master.cents == otherCents
		              && sameTuning(sound.config().voicing.tune, centsTuner(otherCents)),
		          "cents");
		setThrough(sound, "cents", std::to_string(before.voicing.cents));
		const uint8_t otherUnison = before.voicing.unison.count == 4 ? 3 : 4;
		const bool unisonLanded =
		    setThrough(sound, "unison/num", std::to_string(otherUnison))
		    && setThrough(sound, "unison/detune", before.voicing.unison_detune == 20 ? "30" : "20")
		    && setThrough(sound, "unison/spread", before.voicing.unison_spread == 10 ? "15" : "10");
		{
			const deluge::synth::SoundConfig edited = sound.config();
			roundTrip(unisonLanded && edited.voicing.unison.count == otherUnison
			              && instruments::classic::unison(d()).count == otherUnison
			              && edited.voicing.unison_detune == instruments::classic::unison(d()).detune
			              && edited.voicing.unison_spread == instruments::classic::unison(d()).spread,
			          "unison/num, unison/detune and unison/spread");
			sound.deriveConfig();
			for (int32_t u = 0; u < otherUnison; u++) {
				roundTrip(sameTuning(edited.voicing.unison.detunes[u], sound.config().voicing.unison.detunes[u])
				              && edited.voicing.unison.pans[u] == sound.config().voicing.unison.pans[u],
				          "unison tables after the unison");
			}
		}
		setThrough(sound, "unison/spread", std::to_string(before.voicing.unison_spread));
		setThrough(sound, "unison/detune", std::to_string(before.voicing.unison_detune));
		setThrough(sound, "unison/num", std::to_string(before.voicing.unison.count));
		{
			const deluge::effects::EffectStates effectsBefore = sound.effects();
			deluge::model::changeEffects(*sound.effectsOwner(), [&](deluge::effects::EffectStates& e) {
				e.saturation.clipping_amount = effectsBefore.saturation.clipping_amount == 3 ? 4 : 3;
				e.filter.lpf_mode = effectsBefore.filter.lpf_mode == FilterMode::SVF_BAND ? FilterMode::SVF_NOTCH
				                                                                          : FilterMode::SVF_BAND;
				e.filter.hpf_mode =
				    effectsBefore.filter.hpf_mode == FilterMode::HPLADDER ? FilterMode::SVF_BAND : FilterMode::HPLADDER;
				e.filter.route = effectsBefore.filter.route == FilterRoute::PARALLEL ? FilterRoute::HIGH_TO_LOW
				                                                                     : FilterRoute::PARALLEL;
			});
			const deluge::effects::EffectStates& ms = sound.renderEffects();
			roundTrip(ms == sound.effects() && ms.saturation != effectsBefore.saturation
			              && ms.filter.lpf_mode != effectsBefore.filter.lpf_mode
			              && ms.filter.hpf_mode != effectsBefore.filter.hpf_mode
			              && ms.filter.route != effectsBefore.filter.route,
			          "the saturation and filter settings");
			deluge::model::storeEffects(*sound.effectsOwner(), effectsBefore);
		}
	}
	if (modeProbe != nullptr) {
		// Between subtractive and ring mod: neither the filters nor the knobs move with the mode.
		Sound& sound = *modeProbe;
		const auto d = [&sound]() -> const instruments::classic::SoundState& { return *sound.state_; }; // read live
		const SynthMode mode = sound.getSynthMode();
		const SynthMode other = (mode == SynthMode::RINGMOD) ? SynthMode::SUBTRACTIVE : SynthMode::RINGMOD;
		const bool landed = setThrough(sound, "mode", instruments::classic::to_string(other));
		if (!landed || sound.getSynthMode() != other
		    || static_cast<SynthMode>(instruments::classic::synth_mode(d())) != other) {
			std::fprintf(io.err, "M4b FAIL %s: the mode Edit did not write the record and reproject\n", label);
			bad++;
		}
		setThrough(sound, "mode", instruments::classic::to_string(mode));
	}
	if (fmProbe != nullptr) {
		Sound& sound = *fmProbe;
		const auto d = [&sound]() -> const instruments::classic::SoundState& { return *sound.state_; }; // read live
		auto roundTrip = [&](bool ok, const char* setting) {
			if (!ok) {
				std::fprintf(io.err, "M4b FAIL %s: the %s Edit did not write the record and reproject\n", label,
				             setting);
				bad++;
			}
		};
		const deluge::synth::SoundConfig before = sound.config();
		roundTrip(setThrough(sound, "modulator2/toModulator1", before.engine.modulator1_to_modulator0 ? "0" : "1")
		              && sound.config().engine.modulator1_to_modulator0
		                     == instruments::classic::modulator1_to_modulator0(d())
		              && instruments::classic::modulator1_to_modulator0(d()) != before.engine.modulator1_to_modulator0,
		          "modulator2/toModulator1");
		setThrough(sound, "modulator2/toModulator1", before.engine.modulator1_to_modulator0 ? "1" : "0");
		const uint32_t otherPhase = before.engine.modulators[1].retrigger_phase + 5;
		roundTrip(setThrough(sound, "modulator2/retrigPhase", phaseText(otherPhase))
		              && sound.config().engine.modulators[1].retrigger_phase
		                     == instruments::classic::phase_word(instruments::classic::modulators(d())[1].retrigger)
		              && instruments::classic::phase_word(instruments::classic::modulators(d())[1].retrigger)
		                     == otherPhase,
		          "modulator2/retrigPhase");
		setThrough(sound, "modulator2/retrigPhase", phaseText(before.engine.modulators[1].retrigger_phase));
		const int8_t otherModCents = static_cast<int8_t>(before.engine.modulators[1].cents == 30 ? -30 : 30);
		roundTrip(setThrough(sound, "modulator2/cents", std::to_string(otherModCents))
		              && sound.config().engine.modulators[1].cents == otherModCents
		              && instruments::classic::modulators(d())[1].tuning.cents == otherModCents
		              && sameTuning(sound.config().engine.modulators[1].tune, centsTuner(otherModCents)),
		          "modulator2/cents");
		setThrough(sound, "modulator2/cents", std::to_string(before.engine.modulators[1].cents));
	}
	std::fprintf(io.err, "M4b snapshot coherence %s: %d bound sounds, %d mismatches\n", label, sounds, bad);
	return bad;
}

// load(xml) -> fully-loaded Song (currentSong set, view cleared), or nullptr on load error (code in *loadErr).
// `xml` must be writable (the reader null-terminates tokens in place) and outlive all uses of the Song.
static Song* load_song(char* xml, size_t xmlLen, deluge::storage::DocumentFormat format, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);

	// Through the Value tree, as a song load is: parse the whole XML, then replay it.
	std::optional<deluge::storage::Value> tree =
	    parseDocument(std::string_view(reinterpret_cast<const char*>(xml), xmlLen), format);
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
		e = openTreeFile(adapter, "song", format);
		if (e == Error::NONE) {
			e = deluge::bridge::loadSong(*song, adapter);
		}
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr); // drop transient view state (see song_roundtrip.cpp)
	return song;
}

int song_edit_roundtrip_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	const char* songPath = argv[1];

	// Two song-global unpatched params: one gets a distinctive scalar value, the other gets automation nodes.
	// Distinctive non-zero values so they're always serialized and survive the hex round-trip verbatim.
	const int32_t pVal = params::UNPATCHED_REVERB_SEND_AMOUNT;
	const int32_t kSentinel = 0x12345678;
	const int32_t pAuto = params::UNPATCHED_VOLUME;

	FILE* f = std::fopen(songPath, "rb");
	if (!f) {
		std::fprintf(io.err, "cannot open %s\n", songPath);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);

	const uint32_t kCap = 16u * 1024 * 1024;
	char* orig = (char*)std::malloc(fileLen + 1);
	char* gen1 = (char*)std::malloc(kCap);
	char* gen1copy = (char*)std::malloc(kCap);
	char* gen2 = (char*)std::malloc(kCap);     // 2nd-generation save buffer
	char* gen2copy = (char*)std::malloc(kCap); // ...and its reload copy
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	// 1+2. Load, then DOM-first edit the song-global param.
	Error e1;
	Song* song = load_song(orig, fileLen, deluge::storage::formatOfPath(argv[1]), &e1);
	if (!song) {
		std::fprintf(io.err, "LOADFAIL original -> Error %d\n", (int)e1);
		return 1;
	}
	if (checkSoundConfigs(*song, "load") != 0) {
		return 3;
	}

	// Every loaded AudioClip's unpatched set must be BOUND to a DOM store (bindDomStore ran in
	// the post-load loop), and the borrow must be live — engine nodes() aliases the store lane. Run against a
	// song with audio clips (Sampling.XML) to exercise this; songs without audio clips just report seen=0.
	{
		auto isLiveBound = [](ParamSet* s) {
			return s->domStore() != nullptr && s->docForParam(0) == &s->domStore()->params[0];
		};
		auto bothBound = [&](ParamManagerForTimeline& pm) {
			return isLiveBound(pm.getUnpatchedParamSet()) && isLiveBound(pm.getPatchedParamSet());
		};
		// Each cable's depth resolves to its lane, keyed by depth id, in the set's current table (the owner's once
		// relocated, else the set's own).
		auto cablesBound = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				const deluge::plugin::ParamId id =
				    deluge::bridge::cable_depth(pcs->patchCables[c].from, pcs->patchCables[c].destination);
				bool found = false;
				for (const deluge::project::DocCableDepth& lane : pcs->depthStore()) {
					found = found || (lane.depth == id && &lane.amount == pcs->docForParam(c));
				}
				if (!found) {
					return false;
				}
			}
			return true;
		};
		// Each cable's store current_value must equal the engine cursor — the
		// read sources the persisted scalar from the store (docForParam), not the engine.
		auto cableScalarOk = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				if (pcs->docForParam(c)->current_value != pcs->patchCables[c].param.played()) {
					return false;
				}
			}
			return true;
		};
		// The DOM-first read makes the store's current_value authoritative — it must
		// equal the engine cursor for every param. On a no-id song (e.g. Sampling for audio) the post-load
		// engine-seed is skipped, so the store scalar comes ONLY from the read, isolating read-authority; on
		// id songs it also guards the store<->engine consistency the seed/relocate must preserve.
		auto scalarOkSet = [](ParamSet* s) {
			if (s->domStore() == nullptr) {
				return false;
			}
			for (int32_t p = 0; p < s->getNumParams(); p++) {
				const deluge::project::DocAutoParam* lane = s->findDocParamForKey(p);
				if (lane == nullptr || lane->current_value != s->params[p].played()) {
					return false;
				}
			}
			return true;
		};
		auto bothScalarOk = [&](ParamManagerForTimeline& pm) {
			return scalarOkSet(pm.getUnpatchedParamSet()) && scalarOkSet(pm.getPatchedParamSet());
		};
		int32_t audioSeen = 0, audioBound = 0, audioScalarOk = 0, audioIdSeen = 0, audioIdOk = 0;
		int32_t synthSeen = 0, synthBound = 0, synthScalarOk = 0;
		int32_t kitSeen = 0, kitBound = 0, kitScalarOk = 0, drumSeen = 0, drumBound = 0, drumScalarOk = 0;
		int32_t cableSeen = 0, cableBound = 0, cableScalarOkCount = 0;
		// Synth/kit clip-level (INSTRUMENT) + per-drum (DRUM) owners must resolve by durable id
		// (resolveParam == docForParam), i.e. doc() id-resolves them, not the fallback.
		int32_t instIdSeen = 0, instIdOk = 0, drumIdSeen = 0, drumIdOk = 0;
		int32_t midiIdSeen = 0, midiIdOk = 0; // MIDI clip CC automation
		auto instIdResolves = [&](Clip* clip, ParamSet* set) {
			if (!clip->output->id()) {
				return; // an output not yet given its id has no address to resolve
			}
			instIdSeen++;
			deluge::project::ParamAddress addr = deluge::bridge::addressOf(
			    deluge::project::OwnerId::instrument(*clip->output->id()), set->getParamKind(), 0, clip->id());
			instIdOk += (song->project.resolveParam(addr) == set->docForParam(0)) ? 1 : 0;
		};
		std::array<ClipArray*, 2> clipArrays{&song->live.clips.session_clips, &song->live.clips.arrangement_only_clips};
		for (ClipArray* arr : clipArrays) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (!clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				if (clip->type == ClipType::AUDIO) {
					audioSeen++;
					ParamSet* u = clip->paramManager.getUnpatchedParamSet();
					audioBound += isLiveBound(u) ? 1 : 0;
					audioScalarOk += scalarOkSet(u) ? 1 : 0;
					// An audio clip must be id-addressed — resolveParam(clip id) must return the SAME
					// store lane as docForParam, proving doc() id-resolves it (not the fallback).
					audioIdSeen++;
					deluge::project::ParamAddress addr = deluge::bridge::addressOf(
					    deluge::project::OwnerId::clip(clip->id()), u->getParamKind(), 0, std::nullopt);
					audioIdOk += (song->project.resolveParam(addr) == u->docForParam(0)) ? 1 : 0;
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && clip->output->type == OutputType::SYNTH) {
					synthSeen++; // a synth clip binds BOTH its unpatched and patched sets
					synthBound += bothBound(clip->paramManager) ? 1 : 0;
					synthScalarOk += bothScalarOk(clip->paramManager) ? 1 : 0;
					instIdResolves(clip, clip->paramManager.getUnpatchedParamSet());
					if (clip->paramManager.getPatchCableSet()->numPatchCables > 0) {
						cableSeen++; // ...and its patch cables
						cableBound += cablesBound(clip->paramManager) ? 1 : 0;
						cableScalarOkCount += cableScalarOk(clip->paramManager) ? 1 : 0;
					}
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && clip->output->type == OutputType::KIT) {
					kitSeen++; // kit clip-level global FX = unpatched only
					ParamSet* ku = clip->paramManager.getUnpatchedParamSet();
					kitBound += isLiveBound(ku) ? 1 : 0;
					kitScalarOk += scalarOkSet(ku) ? 1 : 0;
					instIdResolves(clip, ku);
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum != nullptr && nr.drum->type == DrumType::SOUND
						    && nr.paramManager.containsAnyMainParamCollections()) {
							drumSeen++; // a SoundDrum row binds unpatched + patched
							drumBound += bothBound(nr.paramManager) ? 1 : 0;
							drumScalarOk += bothScalarOk(nr.paramManager) ? 1 : 0;
							if (nr.drum->id()) {
								drumIdSeen++;
								ParamSet* du = nr.paramManager.getUnpatchedParamSet();
								deluge::project::ParamAddress addr = deluge::bridge::addressOf(
								    deluge::project::OwnerId::drum(*nr.drum->id()), du->getParamKind(), 0, clip->id());
								drumIdOk += (song->project.resolveParam(addr) == du->docForParam(0)) ? 1 : 0;
							}
							if (nr.paramManager.getPatchCableSet()->numPatchCables > 0) {
								cableSeen++; // ...and its patch cables
								cableBound += cablesBound(nr.paramManager) ? 1 : 0;
								cableScalarOkCount += cableScalarOk(nr.paramManager) ? 1 : 0;
							}
						}
					}
				}
				else if (clip->output != nullptr && clip->output->type == OutputType::MIDI_OUT) {
					// A MIDI clip's CC automation must id-resolve (Kind::MIDI, by cc).
					MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
					if (midi != nullptr) {
						for (int32_t i = 0; i < std::ssize(midi->params); i++) {
							midiIdSeen++;
							deluge::project::ParamAddress addr =
							    deluge::bridge::addressOf(deluge::project::OwnerId::clip(clip->id()),
							                              params::Kind::MIDI, midi->params[i].cc, std::nullopt);
							midiIdOk += (song->project.resolveParam(addr) == midi->docForParam(i)) ? 1 : 0;
						}
					}
				}
			}
		}
		std::fprintf(io.err,
		             "audio: %d/%d (scalarOk %d) | synth: %d/%d (scalarOk %d) | kit: %d/%d (scalarOk %d) | "
		             "drums: %d/%d (scalarOk %d) | patchCables: %d/%d (scalarOk %d)\n",
		             audioBound, audioSeen, audioScalarOk, synthBound, synthSeen, synthScalarOk, kitBound, kitSeen,
		             kitScalarOk, drumBound, drumSeen, drumScalarOk, cableBound, cableSeen, cableScalarOkCount);
		if (audioSeen != audioBound || synthSeen != synthBound || kitSeen != kitBound || drumSeen != drumBound
		    || cableSeen != cableBound) {
			std::fprintf(io.err, "PHASE C: a clip/drum loaded UNBOUND (borrow not live)\n");
			return 3;
		}
		if (audioScalarOk != audioSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: an audio clip's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
		std::fprintf(io.err, "audio id-addressed (S5-b.1): %d/%d\n", audioIdOk, audioIdSeen);
		if (audioIdOk != audioIdSeen) {
			std::fprintf(io.err, "S5-b.1 FAIL: an audio clip did not resolve by its id "
			                     "(resolveParam != docForParam) — doc() would hit the engine fallback\n");
			return 3;
		}
		std::fprintf(io.err, "instrument/drum id-addressed (S5-b.3): inst %d/%d, drum %d/%d\n", instIdOk, instIdSeen,
		             drumIdOk, drumIdSeen);
		if (instIdOk != instIdSeen || drumIdOk != drumIdSeen) {
			std::fprintf(io.err, "S5-b.3 FAIL: an instrument/drum owner did not resolve by its id "
			                     "(resolveParam != docForParam) — doc() would hit the engine fallback\n");
			return 3;
		}
		std::fprintf(io.err, "MIDI-clip id-addressed (S5-b.2): %d/%d\n", midiIdOk, midiIdSeen);
		if (midiIdOk != midiIdSeen) {
			std::fprintf(io.err, "S5-b.2 FAIL: a MIDI clip CC did not resolve by its clip's id\n");
			return 3;
		}
		if (kitScalarOk != kitSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: a kit clip's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
		if (synthScalarOk != synthSeen || drumScalarOk != drumSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: a synth/drum clip's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
		if (cableScalarOkCount != cableSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: a patch cable's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
	}

	// Golden only LOADS songs, so it never exercises new-instrument creation
	// (setupAsDefaultSynth / setupAsSample), which sets PERSISTENT params via the setup overload. Save
	// reads the DOM store (toDocParamManagerFromStore), so those setters MUST write the store or a freshly-created
	// synth/sample would save wrong. Build fresh Sounds exactly as createNewInstrument does (setupWithPatching +
	// initParams) and assert store == engine for every param (+ the setup()-created patch cable).
	{
		auto setScalarOk = [](ParamManagerForTimeline& pm) {
			auto ok = [](ParamSet* s) {
				for (int32_t p = 0; p < s->getNumParams(); p++) {
					if (s->docForParam(p)->current_value != s->params[p].played()) {
						return false;
					}
				}
				return true;
			};
			return ok(pm.getUnpatchedParamSet()) && ok(pm.getPatchedParamSet());
		};
		auto cableScalarOk = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				if (pcs->docForParam(c)->current_value != pcs->patchCables[c].param.played()) {
					return false;
				}
			}
			return true;
		};
		// setupAsDefaultSynth — pure param setters.
		{
			auto* si = new SoundInstrument();
			ParamManagerForTimeline pm;
			if (pm.setupWithPatching() != Error::NONE) {
				std::fprintf(io.err, "new-synth pm setup fail\n");
				return 3;
			}
			Sound::initParams(&pm);
			static_cast<Sound*>(si)->setupAsDefaultSynth(&pm);
			if (!setScalarOk(pm)) {
				std::fprintf(io.err, "Flip Option I FAIL: setupAsDefaultSynth store != engine (new-synth save "
				                     "would persist wrong values)\n");
				return 3;
			}
		}
		// setupAsSample — param setters + a velocity->volume patch cable created via PatchCable::setup().
		{
			auto* si = new SoundInstrument();
			ParamManagerForTimeline pm;
			if (pm.setupWithPatching() != Error::NONE) {
				std::fprintf(io.err, "new-sample pm setup fail\n");
				return 3;
			}
			Sound::initParams(&pm);
			static_cast<Sound*>(si)->setupAsSample(&pm);
			if (!setScalarOk(pm) || !cableScalarOk(pm)) {
				std::fprintf(io.err, "Flip Option I FAIL: setupAsSample store != engine (new-sample save would "
				                     "persist wrong values)\n");
				return 3;
			}
		}
		std::fprintf(io.err, "new-instrument setup scalarOk: OK\n");
	}

	// The store is populated by the <songParams> read; seed it from the engine if the song had none.
	if (!unpatchedStore(song->project.master_params)) {
		unpatchedStore(song->project.master_params) =
		    deluge::bridge::toDocParamSet(*song->paramManager.getUnpatchedParamSet());
	}
	deluge::project::DocParamSet& store = *unpatchedStore(song->project.master_params);
	if (pVal >= (int32_t)store.params.size() || pAuto >= (int32_t)store.params.size()) {
		std::fprintf(io.err, "STORE TOO SMALL (%zu params)\n", store.params.size());
		return 3;
	}
	// DOM-first edits on the store: a scalar value + an automation lane (two nodes).
	store.params[pVal].current_value = kSentinel;
	deluge::project::setNodeAtPos(store.params[pAuto], 0, 111, true);
	deluge::project::setNodeAtPos(store.params[pAuto], 48, -222, false);
	// Reproject the store into the engine (+ refresh automation summary flags) so save serializes the edits.
	deluge::bridge::fromDocParamManager(song->project.master_params, song->paramManager);

	// The edits must be visible in the engine immediately.
	AutoParam& engVal = song->paramManager.getUnpatchedParamSet()->params[pVal];
	AutoParam& engAuto = song->paramManager.getUnpatchedParamSet()->params[pAuto];
	if (engVal.played() != kSentinel
	    || song->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes.size() != 2) {
		std::fprintf(io.err, "EDIT DID NOT LAND engVal=%d autoNodes=%zu\n", engVal.played(),
		             song->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes.size());
		return 3;
	}

	// DIVERGE the engine scalar from the store scalar for pVal, so that persistence PROVES the
	// song-global save read the STORE (the song's global-effectable block, holding kSentinel) — NOT the engine
	// cursor. If save still projected the engine, the file would carry this distinct value and the reload assert
	// below (reloaded store == engine == kSentinel) would fail.
	engVal.setPlayed(0x0BADF00D);

	// Session macros name their clip and output by id: aim two at the last session clip and the first output, and
	// require that they name the same clip and output after the reload.
	std::optional<uint32_t> macroClipId;
	std::optional<uint32_t> macroOutputId;
	if (!song->live.clips.session_clips.empty()) {
		Clip* target = song->live.clips.session_clips[song->live.clips.session_clips.size() - 1];
		auto& m = song->project.arrangement.session_macros[6];
		m.kind = deluge::project::SessionMacroKind::CLIP_LAUNCH;
		m.clip_id = target->id();
		macroClipId = target->id();
	}
	if (Output* target = song->live.media.first_output; target != nullptr && target->id().has_value()) {
		auto& m = song->project.arrangement.session_macros[7];
		m.kind = deluge::project::SessionMacroKind::OUTPUT_CYCLE;
		m.output_id = target->id();
		macroOutputId = target->id();
	}

	// An output-cycle macro aimed at an output made after the load (so without an id until something mints one) must
	// still name it after the save and reload. Assigned the way the session view assigns one.
	std::string freshOutputName;
	if (AudioOutput* fresh = song->createNewAudioOutput()) {
		freshOutputName = fresh->name.c_str();
		auto& m = song->project.arrangement.session_macros[5];
		m.kind = deluge::project::SessionMacroKind::OUTPUT_CYCLE;
		m.output_id = song->outputIdFor(*fresh);
	}

	// A section's launch command is data naming its device; it must survive the save and reload, and match incoming
	// MIDI as it did: the same channel and note, from the named device when inputs are told apart by device.
	const deluge::project::MidiCommand sectionCommand{
	    .channel_or_zone = 2,
	    .note_or_cc = 60,
	    .device = deluge::bridge::reference_of(MIDIDeviceManager::dinMIDIPorts)};
	deluge::bridge::setSectionCommand(song->project.mappings, 3, sectionCommand);

	// 3. Save.
	long n1 = deluge::scenarios::driver::saveSongCapture(*song, gen1, kCap);

	// 4. Reload the saved bytes.
	std::memcpy(gen1copy, gen1, n1);
	Error e2;
	Song* reloaded = load_song(gen1copy, n1, deluge::storage::kSongSaveFormat, &e2);
	if (!reloaded) {
		std::fprintf(io.err, "LOADFAIL gen1 -> Error %d\n", (int)e2);
		return 1;
	}
	if (checkSoundConfigs(*reloaded, "reload") != 0) {
		return 3;
	}

	{
		const deluge::project::MidiCommand command = deluge::bridge::sectionCommand(reloaded->project.mappings, 3);
		const bool wasDifferentiating = MIDIDeviceManager::differentiatingInputsByDevice;
		MIDIDeviceManager::differentiatingInputsByDevice = true;
		const bool fromDevice = deluge::bridge::matches(command, MIDIDeviceManager::dinMIDIPorts, 2, 60);
		const bool otherChannel = deluge::bridge::matches(command, MIDIDeviceManager::dinMIDIPorts, 3, 60);
		const bool otherDevice = deluge::bridge::matches(command, MIDIDeviceManager::upstreamUSBMIDICable1, 2, 60);
		MIDIDeviceManager::differentiatingInputsByDevice = false;
		const bool otherDeviceAnyInput =
		    deluge::bridge::matches(command, MIDIDeviceManager::upstreamUSBMIDICable1, 2, 60);
		MIDIDeviceManager::differentiatingInputsByDevice = wasDifferentiating;
		if (command != sectionCommand || !fromDevice || otherChannel || otherDevice || !otherDeviceAnyInput) {
			std::fprintf(io.err,
			             "SECTION FAIL: launch command same=%d fromDevice=%d otherChannel=%d otherDevice=%d "
			             "otherDeviceAnyInput=%d\n",
			             command == sectionCommand, fromDevice, otherChannel, otherDevice, otherDeviceAnyInput);
			return 3;
		}
	}
	// The swing interval is stored relative to the input tick magnitude, whichever of the two the file holds first: a
	// copy of the save with swingInterval moved to the front of the song's attributes reloads to the same groove.
	{
		SavedSong reordered(gen1, n1);
		if (!reordered.moveToFront("swingInterval")) {
			std::fprintf(io.err, "GROOVE FAIL: the save holds no swingInterval\n");
			return 3;
		}
		std::memcpy(gen2copy, reordered.text().data(), reordered.text().size());
		Error reorderedError;
		const deluge::project::Groove expected = reloaded->project.groove;
		Song* reorderedSong =
		    load_song(gen2copy, reordered.text().size(), deluge::storage::kSongSaveFormat, &reorderedError);
		if (!reorderedSong || !(reorderedSong->project.groove == expected)) {
			std::fprintf(io.err, "GROOVE FAIL: swingInterval ahead of inputTickMagnitude reads differently\n");
			return 3;
		}
		currentSong = reloaded;
	}

	// A song file without inputTickMagnitude stores its sync values relative to the magnitude the new song starts
	// with: a copy of the save without it and with swingInterval="7" reads the swing relative to that magnitude.
	{
		SavedSong noMagnitude(gen1, n1);
		if (!noMagnitude.replace("swingInterval", "7") || !noMagnitude.erase("inputTickMagnitude")) {
			std::fprintf(io.err, "GROOVE FAIL: the save holds no inputTickMagnitude or swingInterval\n");
			return 3;
		}
		std::memcpy(gen2copy, noMagnitude.text().data(), noMagnitude.text().size());
		Error noMagnitudeError;
		Song* song2 =
		    load_song(gen2copy, noMagnitude.text().size(), deluge::storage::kSongSaveFormat, &noMagnitudeError);
		if (!song2) {
			std::fprintf(io.err, "GROOVE FAIL: the save without inputTickMagnitude does not load\n");
			return 3;
		}
		const int32_t magnitude = song2->getInputTickMagnitude();
		const int32_t expected = std::min(deluge::project::sync_level_from_file(7, magnitude), int32_t{9});
		if (deluge::plugin::index_of(song2->project.groove.swing_interval) != expected) {
			std::fprintf(io.err,
			             "GROOVE FAIL: without inputTickMagnitude, swingInterval 7 read as %d, not %d (magnitude %d)\n",
			             deluge::plugin::index_of(song2->project.groove.swing_interval), expected, magnitude);
			return 3;
		}
		currentSong = reloaded;
	}

	if (macroClipId) {
		const auto& m = reloaded->project.arrangement.session_macros[6];
		if (m.kind != deluge::project::SessionMacroKind::CLIP_LAUNCH || m.clip_id != macroClipId
		    || reloaded->clipForId(*macroClipId) == nullptr) {
			std::fprintf(io.err, "MACRO FAIL: the clip-launch macro lost its clip %u\n", *macroClipId);
			return 3;
		}
	}
	if (macroOutputId) {
		const auto& m = reloaded->project.arrangement.session_macros[7];
		if (m.kind != deluge::project::SessionMacroKind::OUTPUT_CYCLE || m.output_id != macroOutputId
		    || reloaded->outputForId(*macroOutputId) == nullptr) {
			std::fprintf(io.err, "MACRO FAIL: the output-cycle macro lost its output %u\n", *macroOutputId);
			return 3;
		}
	}

	if (!freshOutputName.empty()) {
		const auto& m = reloaded->project.arrangement.session_macros[5];
		Output* target = m.output_id ? reloaded->outputForId(*m.output_id) : nullptr;
		if (m.kind != deluge::project::SessionMacroKind::OUTPUT_CYCLE || target == nullptr
		    || freshOutputName != target->name.c_str()) {
			std::fprintf(io.err, "MACRO FAIL: the output-cycle macro on a new output (%s) was lost\n",
			             freshOutputName.c_str());
			return 3;
		}
	}

	// A song from before outputs had ids names a macro's output by its position and gives its outputs no ids: the
	// macro must still name the output at that position after the load. Made from this save by taking the ids off
	// the outputs and naming macro 7's output by position.
	if (macroOutputId) {
		SavedSong legacy(gen1, n1);
		if (!legacy.replaceMember(SavedSong::member("outputId", std::to_string(*macroOutputId)),
		                          SavedSong::member("output", "0"))
		    || !legacy.dropOutputIds()) {
			std::fprintf(io.err, "MACRO FAIL: could not make the legacy file\n");
			return 3;
		}
		std::memcpy(gen2copy, legacy.text().data(), legacy.text().size());
		Error legacyError;
		Song* legacySong = load_song(gen2copy, legacy.text().size(), deluge::storage::kSongSaveFormat, &legacyError);
		if (!legacySong) {
			std::fprintf(io.err, "MACRO FAIL: the legacy file did not load (Error %d)\n", (int)legacyError);
			return 3;
		}
		const auto& m = legacySong->project.arrangement.session_macros[7];
		Output* first = legacySong->live.media.first_output;
		if (m.kind != deluge::project::SessionMacroKind::OUTPUT_CYCLE || first == nullptr || !m.output_id
		    || m.output_id != first->id()) {
			std::fprintf(io.err, "MACRO FAIL: a legacy song's output-cycle macro lost its output\n");
			return 3;
		}
		currentSong = reloaded;
	}

	// The save (gen1) assigns ids, so on reload every audio clip's unpatched params must be
	// TREE-resident — its store is the project.clip_params entry keyed by id, not an engine-owned domStore_.
	// (The corpus files carry no ids on first load, so this only fires post-save — proving the tree path is live.)
	{
		int32_t audioClips = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::AUDIO || !clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				audioClips++;
				if (std::optional<uint32_t> id = clip->id()) {
					deluge::project::DocParamManager* mgr = reloaded->project.clip_params.find(*id);
					if (mgr != nullptr && unpatchedStore(*mgr)
					    && clip->paramManager.getUnpatchedParamSet()->domStore() == &*unpatchedStore(*mgr)) {
						treeResident++;
					}
				}
			}
		}
		std::fprintf(io.err, "audio clips tree-resident: %d/%d\n", treeResident, audioClips);
		if (audioClips > 0 && treeResident != audioClips) {
			std::fprintf(io.err, "S2b FAIL: an audio clip is not tree-resident after reload\n");
			return 3;
		}
	}

	// Every reloaded instrument-shared clip (synth or kit clip-level FX) must have its unpatched
	// automation TREE-resident — its store is the project.backed_params entry keyed by
	// (OwnerId::instrument(output id), clip id), not an engine-owned domStore_. Proves the
	// instrument-shared relocation is live (again only post-save, once ids exist).
	{
		int32_t sharedClips = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || (clip->output->type != OutputType::SYNTH && clip->output->type != OutputType::KIT)
				    || !clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				sharedClips++;
				std::optional<uint32_t> clipId = clip->id();
				std::optional<uint32_t> outId = clip->output->id();
				if (clipId.has_value() && outId.has_value()) {
					deluge::project::DocParamManager* mgr =
					    reloaded->project.backed_params.find(deluge::project::OwnerId::instrument(*outId), *clipId);
					if (mgr != nullptr && unpatchedStore(*mgr)
					    && clip->paramManager.getUnpatchedParamSet()->domStore() == &*unpatchedStore(*mgr)) {
						treeResident++;
					}
				}
			}
		}
		std::fprintf(io.err, "instrument-shared clips tree-resident: %d/%d\n", treeResident, sharedClips);
		if (sharedClips > 0 && treeResident != sharedClips) {
			std::fprintf(io.err, "S3c/S3d FAIL: a synth/kit clip is not tree-resident after reload\n");
			return 3;
		}
	}

	// Every reloaded SoundDrum NoteRow's unpatched automation must be TREE-resident — keyed by
	// (OwnerId::drum(drum id), clip id) in project.backed_params. Per-drum params live per (drum, clip)
	// pair (each kit clip's NoteRow has its own manager).
	{
		int32_t drumRows = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::KIT) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
				for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
					NoteRow& nr = ic->noteRows[n];
					if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND
					    || !nr.paramManager.containsAnyMainParamCollections()) {
						continue;
					}
					drumRows++;
					std::optional<uint32_t> drumId = nr.drum->id();
					if (clipId.has_value() && drumId.has_value()) {
						deluge::project::DocParamManager* mgr =
						    reloaded->project.backed_params.find(deluge::project::OwnerId::drum(*drumId), *clipId);
						if (mgr != nullptr && unpatchedStore(*mgr)
						    && nr.paramManager.getUnpatchedParamSet()->domStore() == &*unpatchedStore(*mgr)) {
							treeResident++;
						}
					}
				}
			}
		}
		std::fprintf(io.err, "drum rows tree-resident: %d/%d\n", treeResident, drumRows);
		if (drumRows > 0 && treeResident != drumRows) {
			std::fprintf(io.err, "S3e FAIL: a SoundDrum row is not tree-resident after reload\n");
			return 3;
		}
	}

	// Every reloaded manager that holds an expression (MPE) set — a synth/kit clip-level manager (keyed
	// (instrument(output), clip)) or a SoundDrum NoteRow (keyed (drum, clip)) — must have that set TREE-resident,
	// proving the expression relocation binds at the same (owner, clip) key as unpatched/patched. Counted across
	// both owner tiers; 0/0 on a song with no MPE data is a pass (nothing to relocate).
	{
		int32_t mpeSets = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		auto checkExpression = [&](ParamManager& pm, const deluge::project::OwnerId& owner,
		                           std::optional<uint32_t> clipId) {
			ExpressionParamSet* expression = pm.getExpressionParamSet();
			if (expression == nullptr || !clipId.has_value()) {
				return;
			}
			mpeSets++;
			deluge::project::DocParamManager* mgr = reloaded->project.backed_params.find(owner, clipId);
			if (mgr != nullptr && mgr->expression && expression->domStore() == &*mgr->expression) {
				treeResident++;
			}
		};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || (clip->output->type != OutputType::SYNTH && clip->output->type != OutputType::KIT)) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				std::optional<uint32_t> outId = clip->output->id();
				if (outId.has_value()) {
					checkExpression(clip->paramManager, deluge::project::OwnerId::instrument(*outId), clipId);
				}
				if (clip->output->type == OutputType::KIT) {
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND) {
							continue;
						}
						if (std::optional<uint32_t> drumId = nr.drum->id()) {
							checkExpression(nr.paramManager, deluge::project::OwnerId::drum(*drumId), clipId);
						}
					}
				}
			}
		}
		std::fprintf(io.err, "expression (MPE) sets tree-resident: %d/%d\n", treeResident, mpeSets);
		if (mpeSets > 0 && treeResident != mpeSets) {
			std::fprintf(io.err, "S4a FAIL: an expression set is not tree-resident after reload\n");
			return 3;
		}
	}

	// doc() must id-address kit clip-level FX and per-drum params to the SAME DocAutoParam the bound-store
	// path (docForParam) returns — i.e. doc()'s owner classification (noteRow set ⇒ drum(OwnerId::drum); else
	// synth/kit ⇒ OwnerId::instrument) is correct. Golden/save can't see this (they never resolve via doc()); a
	// misclassification would silently resolve a drum param against the kit-FX entry. Build the REAL ModelStacks the
	// engine uses and assert doc() == docForParam for a param of each kit clip-level manager and each SoundDrum row.
	{
		int32_t checked = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::KIT) {
					continue;
				}
				auto consistent = [&](ParamManagerForTimeline& pm, ModelStackWithAutoParam* msap) -> bool {
					if (msap == nullptr || msap->autoParam == nullptr) {
						return true; // no param at this id → nothing to check
					}
					checked++;
					return msap->doc() == pm.getUnpatchedParamSet()->docForParam(0);
				};
				// Kit clip-level FX (no noteRow): expect an OwnerId::instrument(kit output) address.
				alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE]{};
				ModelStackWithThreeMainThings* m3 = setupModelStackWithThreeMainThingsButNoNoteRow(
				    buf, reloaded, deluge::model::owner_of(*static_cast<Kit*>(clip->output)), clip,
				    &clip->paramManager);
				if (!consistent(clip->paramManager, m3->getUnpatchedAutoParamFromId(0))) {
					std::fprintf(io.err, "S3g FAIL: kit clip-level doc() != bound store (misclassified)\n");
					return 3;
				}
				// Each SoundDrum NoteRow (noteRow set): expect an OwnerId::drum(drum id) address.
				InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
				for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
					NoteRow& nr = ic->noteRows[n];
					if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND
					    || !nr.paramManager.containsAnyMainParamCollections()) {
						continue;
					}
					alignas(ModelStackWithAutoParam) char dbuf[MODEL_STACK_MAX_SIZE]{};
					ModelStackWithThreeMainThings* dm3 = setupModelStackWithThreeMainThingsIncludingNoteRow(
					    dbuf, reloaded, clip, n, &nr, deluge::model::owner_of(*static_cast<SoundDrum*>(nr.drum)),
					    &nr.paramManager);
					if (!consistent(nr.paramManager, dm3->getUnpatchedAutoParamFromId(0))) {
						std::fprintf(io.err, "S3g FAIL: drum doc() != bound store (misclassified as kit-level)\n");
						return 3;
					}
				}
			}
		}
		std::fprintf(io.err, "kit/drum doc()==bound-store checks: %d\n", checked);
	}

	// Every reloaded synth/drum manager's patch cables must be (a) tree-resident — the engine cable set
	// bound to the owner's project-tree `cables` — and (b) id-addressable: the address doc() builds for each
	// cable (owner + PATCH_CABLE + the packed (dest,source) paramId) must resolve to the SAME DocAutoParam the
	// engine bound-store path (docForParam) returns. Identity-matched, so this catches a mis-encoded/mis-owned
	// cable that save (which never resolves via doc()) would silently miss.
	{
		int32_t cableSets = 0, treeResident = 0, addrChecks = 0;
		bool misOwned = false;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		auto checkCables = [&](ParamManagerForTimeline& pm, const deluge::project::OwnerId& owner,
		                       std::optional<uint32_t> clipId) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			if (pcs == nullptr || !clipId.has_value()) {
				return;
			}
			cableSets++;
			deluge::project::DocParamManager* mgr = reloaded->project.backed_params.find(owner, clipId);
			if (mgr != nullptr && &pcs->depthStore() == &mgr->cable_depths) {
				treeResident++;
			}
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				int32_t packedId = PatchCableSet::getParamId(pcs->patchCables[c].destination, pcs->patchCables[c].from);
				deluge::project::ParamAddress addr =
				    deluge::bridge::addressOf(owner, deluge::modulation::params::Kind::PATCH_CABLE, packedId, clipId);
				if (reloaded->project.resolveParam(addr) != pcs->docForParam(c)) {
					misOwned = true;
					return;
				}
				addrChecks++;
			}
		};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				if (clip->output->type == OutputType::SYNTH) {
					if (std::optional<uint32_t> outId = clip->output->id()) {
						checkCables(clip->paramManager, deluge::project::OwnerId::instrument(*outId), clipId);
					}
				}
				else if (clip->output->type == OutputType::KIT) {
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND) {
							continue;
						}
						if (std::optional<uint32_t> drumId = nr.drum->id()) {
							checkCables(nr.paramManager, deluge::project::OwnerId::drum(*drumId), clipId);
						}
					}
				}
			}
		}
		if (misOwned) {
			std::fprintf(io.err, "S4b FAIL: patch-cable doc() != bound store (mis-encoded/mis-owned)\n");
			return 3;
		}
		std::fprintf(io.err, "patch-cable sets tree-resident: %d/%d (doc()==bound-store checks: %d)\n", treeResident,
		             cableSets, addrChecks);
		if (cableSets > 0 && treeResident != cableSets) {
			std::fprintf(io.err, "S4b FAIL: a synth/drum patch-cable set is not tree-resident after reload\n");
			return 3;
		}
	}

	// Every reloaded MIDI_OUT clip's CC automation must be (a) tree-resident — the engine
	// MIDIParamCollection bound to the clip's per-clip project-tree midi_params (keyed by Clip::id, in
	// clip_params like audio clips, since MIDI CC automation is per-clip) — and (b) id-addressable: the address
	// doc() builds (OwnerId::clip + Kind::MIDI + cc) must resolve to the SAME DocAutoParam the engine bound-store
	// path (docForParam) returns. Catches a mis-keyed cc that save (never via doc()) would silently miss.
	{
		int32_t midiSets = 0, treeResident = 0, addrChecks = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::MIDI_OUT
				    || !clip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				if (!clipId.has_value()) {
					continue;
				}
				MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
				midiSets++;
				deluge::project::DocParamManager* mgr = reloaded->project.clip_params.find(*clipId);
				if (mgr != nullptr && midi->domMidi() == &mgr->midi_params) {
					treeResident++;
				}
				for (int32_t i = 0; i < std::ssize(midi->params); i++) {
					int32_t cc = midi->params[i].cc;
					deluge::project::ParamAddress addr =
					    deluge::bridge::addressOf(deluge::project::OwnerId::clip(*clipId),
					                              deluge::modulation::params::Kind::MIDI, cc, std::nullopt);
					if (reloaded->project.resolveParam(addr) != midi->docForParam(i)) {
						std::fprintf(io.err, "S4c FAIL: MIDI doc() != bound store (mis-keyed cc)\n");
						return 3;
					}
					addrChecks++;
				}
			}
		}
		std::fprintf(io.err, "MIDI CC sets tree-resident: %d/%d (doc()==bound-store checks: %d)\n", treeResident,
		             midiSets, addrChecks);
		if (midiSets > 0 && treeResident != midiSets) {
			std::fprintf(io.err, "S4c FAIL: a MIDI_OUT CC set is not tree-resident after reload\n");
			return 3;
		}
	}

	// Data-loss regression guard: a runtime automation edit routes through doc() -> the TREE store,
	// leaving the patch-cable set's own lanes / the MIDI inline `nodes_` STALE. The SAVE projection must read the
	// tree (docForParam), not the inline store, or the edit is silently lost. Corpus files carry no ids on
	// first load (nothing is bound then), so the bound tree exists only on the RELOADED song — plant a distinctive
	// node in a bound patch cable's and a bound MIDI CC's tree store here (via docForParam, exactly where doc()
	// writes), leave the inline stale, then save+reload again and require it survived.
	{
		constexpr int32_t kMarkPos = 13;
		constexpr int32_t kMarkVal = 0x5A5A5A;
		bool plantedCable = false, plantedMidi = false;
		std::optional<uint32_t> cableClipId, midiClipId;

		auto scanCableForMark = [](Clip* clip) -> bool {
			PatchCableSet* pcs = clip->paramManager.getPatchCableSet();
			for (int32_t c = 0; pcs != nullptr && c < pcs->numPatchCables; c++) {
				const auto& nodes = pcs->docForParam(c)->nodes;
				for (int32_t n = 0; n < std::ssize(nodes); n++) {
					if (nodes[n].pos == kMarkPos && nodes[n].value == kMarkVal) {
						return true;
					}
				}
			}
			return false;
		};
		auto scanMidiForMark = [](Clip* clip) -> bool {
			MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
			for (int32_t i = 0; midi != nullptr && i < std::ssize(midi->params); i++) {
				const auto& nodes = midi->docForParam(i)->nodes;
				for (int32_t n = 0; n < std::ssize(nodes); n++) {
					if (nodes[n].pos == kMarkPos && nodes[n].value == kMarkVal) {
						return true;
					}
				}
			}
			return false;
		};

		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr) {
					continue;
				}
				if (!plantedCable && clip->output->type == OutputType::SYNTH) {
					PatchCableSet* pcs = clip->paramManager.getPatchCableSet();
					if (pcs != nullptr && pcs->numPatchCables > 0 && pcs->depthsRelocated()) {
						deluge::project::setNodeAtPos(*pcs->docForParam(0), kMarkPos, kMarkVal, false);
						cableClipId = clip->id();
						plantedCable = true;
					}
				}
				if (!plantedMidi && clip->output->type == OutputType::MIDI_OUT
				    && clip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
					MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
					if (midi != nullptr && !midi->params.empty() && midi->domMidi() != nullptr) {
						deluge::project::setNodeAtPos(*midi->docForParam(0), kMarkPos, kMarkVal, false);
						midiClipId = clip->id();
						plantedMidi = true;
					}
				}
			}
		}

		if (plantedCable || plantedMidi) {
			long n2 = deluge::scenarios::driver::saveSongCapture(*reloaded, gen2, kCap);
			std::memcpy(gen2copy, gen2, n2);
			Error e3;
			Song* reloaded2 = load_song(gen2copy, n2, deluge::storage::kSongSaveFormat, &e3);
			if (reloaded2 == nullptr) {
				std::fprintf(io.err, "S6-fix: LOADFAIL gen2 -> Error %d\n", (int)e3);
				return 1;
			}
			auto findClip = [&](std::optional<uint32_t> id) -> Clip* {
				if (!id.has_value()) {
					return nullptr;
				}
				std::array<ClipArray*, 2> a{&reloaded2->live.clips.session_clips,
				                            &reloaded2->live.clips.arrangement_only_clips};
				for (ClipArray* arr : a) {
					for (int32_t c = 0; c < std::ssize(*arr); c++) {
						if ((*arr)[c]->id() == id) {
							return (*arr)[c];
						}
					}
				}
				return nullptr;
			};
			if (plantedCable) {
				Clip* rc = findClip(cableClipId);
				if (rc == nullptr || !scanCableForMark(rc)) {
					std::fprintf(io.err, "S6-fix FAIL: patch-cable automation edit LOST on save (read the set's own "
					                     "lanes, not the tree)\n");
					return 3;
				}
			}
			if (plantedMidi) {
				Clip* rc = findClip(midiClipId);
				if (rc == nullptr || !scanMidiForMark(rc)) {
					std::fprintf(io.err, "S6-fix FAIL: MIDI CC automation edit LOST on save (writeToFile read stale "
					                     "nodes_, not the tree)\n");
					return 3;
				}
			}
			std::fprintf(io.err, "S6-fix: tree-authoritative save OK (cable=%d midi=%d)\n", plantedCable ? 1 : 0,
			             plantedMidi ? 1 : 0);
		}
	}

	// Ops regression guard: MIDIParamCollection's automation ops (stripAutomation/generateRepeats/
	// shiftHorizontally/...) must operate on the tree store (docForParam) when bound, not the stale inline nodes_.
	// Representative check: plant a node in a bound MIDI CC's tree, call stripAutomation(), and require the TREE
	// lane is cleared. Clearing the inline nodes_ instead would leave the tree untouched.
	{
		std::array<ClipArray*, 2> arrs{&reloaded->live.clips.session_clips,
		                               &reloaded->live.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::MIDI_OUT
				    || !clip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
					continue;
				}
				MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
				if (midi == nullptr || midi->params.empty() || midi->domMidi() == nullptr) {
					continue;
				}
				deluge::project::setNodeAtPos(*midi->docForParam(0), 21, 0x0B0B0B, false);
				if (midi->docForParam(0)->nodes.size() == 0) {
					std::fprintf(io.err, "S6-fix ops: planted node missing (setup error)\n");
					return 3;
				}
				midi->stripAutomation();
				if (midi->docForParam(0)->nodes.size() != 0) {
					std::fprintf(io.err, "S6-fix FAIL: MIDI stripAutomation did not clear the TREE lane (op read "
					                     "stale inline nodes_)\n");
					return 3;
				}
				std::fprintf(io.err, "S6-fix ops: MIDI stripAutomation hit the tree OK\n");
				goto opsChecked;
			}
		}
opsChecked:;
	}

	// 5. Both edits must have persisted into the reloaded DOM store (the authority) and the reloaded engine.
	if (!unpatchedStore(reloaded->project.master_params)) {
		std::fprintf(io.err, "reloaded store EMPTY\n");
		return 3;
	}
	deluge::project::DocParamSet& rstore = *unpatchedStore(reloaded->project.master_params);
	int32_t storeVal = rstore.params[pVal].current_value;
	int32_t engineVal = reloaded->paramManager.getUnpatchedParamSet()->params[pVal].played();

	const auto& storeNodes = rstore.params[pAuto].nodes;
	const auto& engNodes = reloaded->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes;

	bool valOk = (storeVal == kSentinel) && (engineVal == kSentinel);
	bool autoStoreOk = storeNodes.size() == 2 && storeNodes[0].pos == 0 && storeNodes[0].value == 111
	                   && storeNodes[0].interpolated && storeNodes[1].pos == 48 && storeNodes[1].value == -222
	                   && !storeNodes[1].interpolated;
	bool autoEngOk = engNodes.size() == 2 && engNodes[0].value == 111 && engNodes[1].value == -222;

	bool persisted = valOk && autoStoreOk && autoEngOk;
	std::fprintf(io.err,
	             "value p=%d sentinel=0x%08X reloaded store=0x%08X engine=0x%08X | automation p=%d storeNodes=%zu "
	             "engNodes=%zu | %s\n",
	             pVal, kSentinel, storeVal, engineVal, pAuto, storeNodes.size(), engNodes.size(),
	             persisted ? "PERSISTED" : "LOST");
	return persisted ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(song_edit_roundtrip, song_edit_roundtrip_main)
