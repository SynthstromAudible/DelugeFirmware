#pragma once

/// @file
/// @brief The setting rows a menu shows, the named skips, and the checks that compare each row with the reference
///        rows of `tests/golden/ui/menu_oracle/descriptors.tsv`: what `descriptor_oracle_spec.cpp`,
///        `setting_names_spec.cpp` and `descriptor_coherence_spec.cpp` all walk.
///
/// @warning Include this BEFORE `<cppspec.hpp>` (see `card_access.h`); it does so itself, in that order.

#include "effects/settings.h"
#include "gui/ui/menu_view_of.h"
#include "instruments/classic/settings.h"
#include "plugin/types/arp.h"
#include "plugin/types/setting.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/setting_names.h"
#include "gui/l10n/short_label.h"
#include "gui/l10n/strings.h"
#include "memory/heaps.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/arp_setting_for.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/setting_projection.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "project/setting_group.h"
#include "ui/scenes/menu/context.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/menu_engine.h"
#include "support/oracle_table.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;

/// The cases and rigs, in a named namespace (and inline) so every spec that includes this header shares one copy.
namespace descriptor_cases {

using deluge::plugin::from_menu;
using deluge::plugin::menu_range;
using deluge::plugin::SettingContext;
using deluge::plugin::SettingInfo;
using deluge::plugin::SettingKind;
using deluge::plugin::to_menu;
using deluge::serialization::Value;
using deluge::ui::scenes::menu::MenuContext;
using Owner = SettingContext::Owner;
using Range = std::pair<int32_t, int32_t>;

// ---------------------------------------------------------------------------------------------
// The fixtures and the contexts.

/// Point the song and the clip at what @p ctx edits.
inline void install(const MenuContext& ctx, Song* song) {
	currentSong = song;
	const spec::MenuEngine engine = spec::menu_engine(ctx, *song);
	if (engine.clip != nullptr) {
		song->setCurrentClip(engine.clip);
	}
	currentUIMode = 0;
}

/// Where a case's row is shown, and what the sound there is set up as first.
enum class Where : uint8_t {
	SYNTH,         ///< a synth track, subtractive, both oscillators square
	SYNTH_SAMPLE,  ///< a synth track, subtractive, both oscillators samples
	SYNTH_FM,      ///< a synth track in FM
	SYNTH_RINGMOD, ///< a synth track in ring mod, both oscillators square
	KIT_LANE,      ///< a kit lane's sound, subtractive
	KIT,           ///< the whole kit (affect-entire): its own effects and arp
	KIT_GATE_ROW,  ///< the whole kit, with a gate row selected: legacy reads that row's kind
	MIDI_DRUM,     ///< a kit's MIDI row (its arp's host rows are settings there)
	GATE_DRUM,     ///< a kit's gate row
	MIDI_CLIP,     ///< a MIDI clip (its arp's host rows are settings there)
	CV_CLIP,       ///< a CV clip, both CV channels in use
	AUDIO_CLIP,    ///< an audio clip
	SONG,          ///< the song's own settings
};

struct Synth {
	spec::SongFixture song;
	MenuContext ctx{};
};
inline Synth& synth() {
	static Synth s = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		Synth r;
		r.song = spec::make_synth_song();
		currentSong = r.song.song; // the app binds through the song it plays
		r.ctx = deluge::ui::scenes::spec::ctx_of(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
		                                         false, false);
		return r;
	}();
	return s;
}

struct KitLane {
	spec::SongFixture song;
	SoundDrum* drum = nullptr;
	MenuContext ctx{};
};
inline KitLane& kit_lane() {
	static KitLane k = [] {
		KitLane r;
		r.song = spec::make_kit_song();
		r.drum = spec::add_drum<SoundDrum>(r.song);
		static_cast<Kit*>(r.song.instrument)->selectedDrum = r.drum;
		currentSong = r.song.song; // the app binds through the song it plays
		r.ctx = deluge::ui::scenes::spec::ctx_of(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
		                                         false, false);
		return r;
	}();
	return k;
}

/// A kit whose one row is a non-sound drum of type @p T, selected.
template <typename T>
struct NonSoundRow {
	spec::SongFixture song;
	T* drum = nullptr;
};
template <typename T>
NonSoundRow<T>& non_sound_row() {
	static NonSoundRow<T> r = [] {
		NonSoundRow<T> out;
		out.song = spec::make_kit_song();
		out.drum = spec::add_drum<T>(out.song);
		return out;
	}();
	return r;
}

struct MidiClip {
	spec::SongFixture song;
	MenuContext ctx{};
};
inline MidiClip& midi_clip() {
	static MidiClip m = [] {
		MidiClip r;
		r.song = spec::make_midi_song();
		currentSong = r.song.song; // the app binds through the song it plays
		r.ctx = deluge::ui::scenes::spec::ctx_of(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
		                                         false, false);
		return r;
	}();
	return m;
}

struct CvClip {
	spec::SongFixture song;
	MenuContext ctx{};
};
inline CvClip& cv_clip() {
	static CvClip c = [] {
		CvClip r;
		r.song = spec::make_cv_song();
		currentSong = r.song.song; // the app binds through the song it plays
		r.ctx = deluge::ui::scenes::spec::ctx_of(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
		                                         false, false);
		return r;
	}();
	return c;
}

struct AudioClipRig {
	spec::AudioSongFixture song;
	MenuContext ctx{};
};
inline AudioClipRig& audio_clip() {
	static AudioClipRig a = [] {
		AudioClipRig r;
		r.song = spec::make_audio_clip_song();
		currentSong = r.song.song; // the app binds through the song it plays
		r.ctx = deluge::ui::scenes::spec::ctx_of(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
		                                         false, false);
		return r;
	}();
	return a;
}

/// Set @p sound's setting at @p path to the word or number @p word.
inline void sound_setting(Sound& sound, std::string_view path, std::string_view word) {
	deluge::edits::settingFor(sound, path, Value::makeScalar(std::string(word)));
}

/// The song @p where's rig is in.
inline Song& song_of(Where where);

/// Resolve the context @p where's rig is in now, as `SoundEditor::setup` would, install it and return it. The song
/// is its own root here (the song view's), where `enter` edits the song from the audio clip's.
inline const MenuContext& settle(Where where) {
	static MenuContext ctx;
	const auto clip_of = [](Where w) -> Clip* {
		switch (w) {
		case Where::KIT_LANE:
		case Where::KIT:
			return kit_lane().song.clip;
		case Where::MIDI_DRUM:
			return non_sound_row<MIDIDrum>().song.clip;
		case Where::GATE_DRUM:
		case Where::KIT_GATE_ROW:
			return non_sound_row<GateDrum>().song.clip;
		case Where::MIDI_CLIP:
			return midi_clip().song.clip;
		case Where::CV_CLIP:
			return cv_clip().song.clip;
		case Where::AUDIO_CLIP:
			return audio_clip().song.clip;
		case Where::SONG:
			return nullptr;
		default:
			return synth().song.clip;
		}
	};
	Song& song = song_of(where);
	const bool song_root = where == Where::SONG;
	currentSong = &song; // the app binds through the song it plays
	ctx = deluge::ui::scenes::spec::ctx_of(song, deluge::gui::menuViewOf(clip_of(where)), 0, false, song_root,
	                                       song_root, where == Where::KIT || where == Where::KIT_GATE_ROW);
	install(ctx, &song);
	return ctx;
}

/// Set up the sound @p where names, install its context and return it.
inline const MenuContext& enter(Where where, int8_t source = 0) {
	switch (where) {
	case Where::SYNTH:
	case Where::SYNTH_SAMPLE:
	case Where::SYNTH_FM:
	case Where::SYNTH_RINGMOD: {
		Synth& s = synth();
		Sound& sound = *spec::menu_engine(s.ctx, *s.song.song).sound;
		sound_setting(sound, "mode", "subtractive");
		const std::string_view type = where == Where::SYNTH_SAMPLE ? "sample" : "square";
		sound_setting(sound, "osc1/type", type);
		sound_setting(sound, "osc2/type", type);
		if (where == Where::SYNTH_FM) {
			sound_setting(sound, "mode", "fm");
		}
		if (where == Where::SYNTH_RINGMOD) {
			sound_setting(sound, "mode", "ringmod");
		}
		currentSong = s.song.song; // the app binds through the song it plays
		s.ctx = deluge::ui::scenes::spec::ctx_of(*s.song.song, deluge::gui::menuViewOf(s.song.clip), source, false,
		                                         false, false, false);
		install(s.ctx, s.song.song);
		return s.ctx;
	}
	case Where::KIT_LANE: {
		KitLane& k = kit_lane();
		sound_setting(*k.drum, "mode", "subtractive");
		currentSong = k.song.song; // the app binds through the song it plays
		k.ctx = deluge::ui::scenes::spec::ctx_of(*k.song.song, deluge::gui::menuViewOf(k.song.clip), source, false,
		                                         false, false, false);
		install(k.ctx, k.song.song);
		return k.ctx;
	}
	case Where::KIT:
	case Where::KIT_GATE_ROW:
	case Where::MIDI_DRUM:
	case Where::GATE_DRUM:
		return settle(where);
	case Where::MIDI_CLIP: {
		MidiClip& m = midi_clip();
		install(m.ctx, m.song.song);
		return m.ctx;
	}
	case Where::CV_CLIP: {
		CvClip& v = cv_clip();
		install(v.ctx, v.song.song);
		return v.ctx;
	}
	case Where::AUDIO_CLIP:
	case Where::SONG: {
		AudioClipRig& a = audio_clip();
		install(a.ctx, a.song.song);
		return a.ctx;
	}
	}
	return synth().ctx;
}

inline Song& song_of(Where where) {
	switch (where) {
	case Where::KIT_LANE:
	case Where::KIT:
		return *kit_lane().song.song;
	case Where::MIDI_DRUM:
		return *non_sound_row<MIDIDrum>().song.song;
	case Where::GATE_DRUM:
	case Where::KIT_GATE_ROW:
		return *non_sound_row<GateDrum>().song.song;
	case Where::MIDI_CLIP:
		return *midi_clip().song.song;
	case Where::CV_CLIP:
		return *cv_clip().song.song;
	case Where::AUDIO_CLIP:
	case Where::SONG:
		return *audio_clip().song.song;
	default:
		return *synth().song.song;
	}
}

// ---------------------------------------------------------------------------------------------
// A row's stored value, set through its Edit and read back from its record.

/// Which table a row comes from, and so how its value is set and read.
enum class Family : uint8_t {
	INSTRUMENT,
	EFFECT,
	ARP,
	ARP_HOST,
	AUDIO_CLIP,
	SONG,
	CLIP_VALUES,
	SOUND_ROUTING,
	// The groups whose rows are named but not driven by the range, read and write checks.
	NOTE_ROW,
	CLIP,
	INSTRUMENT_DEVICE,
	AUDIO_INPUT,
};

/// Whether @p where is a kit row (a drum), whose scope is one kit lane.
inline bool is_kit_lane(Where where) {
	return where == Where::KIT_LANE || where == Where::MIDI_DRUM || where == Where::GATE_DRUM;
}

/// The descriptor context the host passes for @p family's rows at @p where, under SettingContext's host contract: the
/// scope of the family's group (a track's, a kit lane's, the whole kit's own effects, a clip's or the song's) and what
/// plays there. Which sources hold a loaded file is read off the sound by the caller.
/// @throws std::logic_error when the pair is not coherent.
inline SettingContext context_of(Where where, Family family) {
	using Instrument = SettingContext::Instrument;
	SettingContext shown{};
	switch (where) {
	case Where::KIT_LANE:
		shown.instrument = Instrument::SOUND_DRUM;
		break;
	case Where::MIDI_DRUM:
		shown.instrument = Instrument::MIDI_DRUM;
		break;
	case Where::GATE_DRUM:
		shown.instrument = Instrument::GATE_DRUM;
		break;
	case Where::KIT:
	case Where::KIT_GATE_ROW:
		shown.instrument = Instrument::KIT;
		break;
	case Where::MIDI_CLIP:
		shown.instrument = Instrument::MIDI;
		break;
	case Where::CV_CLIP:
		shown.instrument = Instrument::CV;
		break;
	case Where::AUDIO_CLIP:
		shown.instrument = Instrument::AUDIO;
		break;
	case Where::SONG:
		shown.instrument = Instrument::NONE;
		break;
	default:
		shown.instrument = Instrument::SYNTH;
		break;
	}
	switch (family) {
	case Family::SONG:
		shown.owner = Owner::SONG;
		break;
	case Family::EFFECT:
		// A synth's or an audio track's effects are the track's; the whole kit's own are the kit's.
		shown.owner = where == Where::SONG                                    ? Owner::SONG
		              : is_kit_lane(where)                                    ? Owner::KIT_LANE
		              : (where == Where::KIT || where == Where::KIT_GATE_ROW) ? Owner::KIT
		                                                                      : Owner::TRACK;
		break;
	case Family::INSTRUMENT:
	case Family::SOUND_ROUTING:
	case Family::NOTE_ROW:
	case Family::ARP:
	case Family::ARP_HOST:
		// A kit row's own sound, routing, note row and arp are the lane's; a synth's sound and routing the track's; a
		// synth's arp and the whole kit's are the clip's.
		shown.owner = is_kit_lane(where)                                                ? Owner::KIT_LANE
		              : family == Family::INSTRUMENT || family == Family::SOUND_ROUTING ? Owner::TRACK
		                                                                                : Owner::CLIP;
		break;
	case Family::AUDIO_CLIP:
	case Family::CLIP_VALUES:
	case Family::CLIP:
		shown.owner = Owner::CLIP;
		break;
	case Family::INSTRUMENT_DEVICE:
	case Family::AUDIO_INPUT:
		shown.owner = Owner::TRACK;
		break;
	}
	shown.audio_inputs_present = AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
	if (!deluge::plugin::context_is_coherent(shown)) {
		throw std::logic_error("a setting context whose owner and instrument do not pair");
	}
	return shown;
}

inline std::span<const SettingInfo> table_of(Family family) {
	switch (family) {
	case Family::INSTRUMENT:
		return deluge::instruments::classic::kSettings;
	case Family::EFFECT:
		return deluge::effects::kEffectSettings;
	case Family::ARP:
		return deluge::plugin::kArpSettingRows;
	case Family::ARP_HOST:
		return deluge::plugin::kArpHostRows;
	case Family::AUDIO_CLIP:
		return deluge::project::table_of(deluge::project::SettingGroupId::AUDIO_CLIP);
	case Family::SONG:
		return deluge::project::table_of(deluge::project::SettingGroupId::SONG);
	case Family::CLIP_VALUES:
		return deluge::project::table_of(deluge::project::SettingGroupId::CLIP_VALUES);
	case Family::NOTE_ROW:
		return deluge::project::table_of(deluge::project::SettingGroupId::NOTE_ROW);
	case Family::CLIP:
		return deluge::project::table_of(deluge::project::SettingGroupId::CLIP);
	case Family::INSTRUMENT_DEVICE:
		return deluge::project::table_of(deluge::project::SettingGroupId::INSTRUMENT_DEVICE);
	case Family::AUDIO_INPUT:
		return deluge::project::table_of(deluge::project::SettingGroupId::AUDIO_INPUT);
	case Family::SOUND_ROUTING:
		return deluge::project::table_of(deluge::project::SettingGroupId::SOUND_ROUTING);
	}
	return {};
}

/// @p stored in its file form: a CHOICE's word, else the number.
inline Value file_form(const SettingInfo& row, int32_t stored) {
	if (row.kind == SettingKind::CHOICE) {
		return Value::makeScalar(std::string(row.choices[static_cast<size_t>(stored)]));
	}
	return Value::makeNumber(stored);
}

/// @p value as the row stores it: a CHOICE's index, else the number.
inline std::optional<int32_t> stored_form(const SettingInfo& row, const Value& value) {
	const std::string text = value.str();
	if (row.kind == SettingKind::CHOICE) {
		for (size_t i = 0; i < row.choices.size(); ++i) {
			if (row.choices[i] == text) {
				return static_cast<int32_t>(i);
			}
		}
		return std::nullopt;
	}
	const std::optional<int64_t> n = deluge::plugin::whole_number(text);
	return n ? std::optional<int32_t>{static_cast<int32_t>(*n)} : std::nullopt;
}

/// An arp's stored value at @p path, read from the engine's arp as the record projects it.
inline std::optional<int32_t> arp_field(const ArpeggiatorSettings& arp, std::string_view path) {
	const std::string_view name = path.substr(std::string_view{"arpeggiator/"}.size());
	const auto sync_type_choice = [](SyncType type) {
		return type == SYNC_TYPE_TRIPLET ? 1 : type == SYNC_TYPE_DOTTED ? 2 : 0;
	};
	const std::pair<std::string_view, int64_t> fields[] = {
	    {"arpMode", std::to_underlying(arp.mode)},
	    {"octaveMode", std::to_underlying(arp.octaveMode)},
	    {"noteMode", std::to_underlying(arp.noteMode)},
	    {"mpeVelocity", std::to_underlying(arp.mpeVelocity)},
	    {"numOctaves", arp.numOctaves},
	    {"stepRepeat", arp.numStepRepeats},
	    {"chordType", arp.chordTypeIndex},
	    {"syncLevel", std::to_underlying(arp.syncLevel)},
	    {"syncType", sync_type_choice(arp.syncType)},
	    {"randomizerLock", arp.randomizerLock ? 1 : 0},
	    {"kitArp", arp.includeInKitArp ? 1 : 0},
	    {"preset", std::to_underlying(arp.preset)},
	    {"gate", arp.values[deluge::arp::param::kGate]},
	    {"rate", arp.values[deluge::arp::param::kRate]},
	    {"rhythm", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kRhythm))},
	    {"sequenceLength", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kSequenceLength))},
	    {"chordPolyphony", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kChordPolyphony))},
	    {"ratchetAmount", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kRatchetAmount))},
	    {"noteProbability", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kNoteProbability))},
	    {"bassProbability", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kBassProbability))},
	    {"swapProbability", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kSwapProbability))},
	    {"glideProbability", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kGlideProbability))},
	    {"chordProbability", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kChordProbability))},
	    {"ratchetProbability", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kRatchetProbability))},
	    {"spreadVelocity", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kSpreadVelocity))},
	    {"spreadGate", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kSpreadGate))},
	    {"spreadOctave", static_cast<int32_t>(arp.values.unsigned_word(deluge::arp::param::kSpreadOctave))},
	};
	for (const auto& [field, value] : fields) {
		if (field == name) {
			return static_cast<int32_t>(value);
		}
	}
	return std::nullopt;
}

/// Set @p row's stored value through its Edit, on what @p ctx edits.
inline void set_stored(Family family, const MenuContext& ctx, Where where, const SettingInfo& row, int32_t stored) {
	const Value value = file_form(row, stored);
	switch (family) {
	case Family::INSTRUMENT:
		deluge::edits::settingFor(*spec::menu_engine(ctx, song_of(where)).sound, row.path, value);
		return;
	case Family::EFFECT:
		deluge::edits::effectSettingFor(deluge::ui::scenes::spec::menu_engine(ctx, song_of(where)).mod, row.path,
		                                value);
		return;
	case Family::ARP:
	case Family::ARP_HOST:
		deluge::edits::arpSettingFor(*spec::menu_engine(ctx, song_of(where)).arp, row.path, value);
		return;
	case Family::AUDIO_CLIP:
		deluge::edits::audioClipSettingFor(*audio_clip().song.clip, row.path, value);
		return;
	case Family::SONG:
		deluge::edits::songSettingFor(row.path, value);
		return;
	case Family::CLIP_VALUES:
		deluge::edits::clipValueSettingFor(*static_cast<InstrumentClip*>(spec::menu_engine(ctx, song_of(where)).clip),
		                                   row.path, value);
		return;
	case Family::NOTE_ROW: {
		auto& clip = *static_cast<InstrumentClip*>(spec::menu_engine(ctx, song_of(where)).clip);
		deluge::edits::noteRowSettingFor(*clip.getNoteRowForDrum(kit_lane().drum), clip, row.path, value);
		return;
	}
	case Family::CLIP:
		deluge::edits::clipSettingFor(*spec::menu_engine(ctx, song_of(where)).clip, row.path, value);
		return;
	case Family::INSTRUMENT_DEVICE:
		deluge::edits::trackSettingFor(*spec::menu_engine(ctx, song_of(where)).output, row.path, value);
		return;
	case Family::AUDIO_INPUT:
		deluge::edits::audioInputSettingFor(static_cast<AudioOutput&>(*spec::menu_engine(ctx, song_of(where)).output),
		                                    row.path, value);
		return;
	case Family::SOUND_ROUTING:
		deluge::edits::routingSettingFor(*spec::menu_engine(ctx, song_of(where)).sound, row.path, value);
		return;
	}
	(void)where;
}

/// @p row's stored value on what @p ctx edits, or nothing where the row does not apply.
inline std::optional<int32_t> get_stored(Family family, const MenuContext& ctx, Where where, const SettingInfo& row) {
	std::optional<Value> value;
	switch (family) {
	case Family::INSTRUMENT:
		value = deluge::instruments::classic::setting_value(spec::menu_engine(ctx, song_of(where)).sound->state(),
		                                                    row.path);
		break;
	case Family::EFFECT:
		value = deluge::effects::effect_setting_value(
		    deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(ctx, song_of(where)).mod)->effects(),
		    row.path);
		break;
	case Family::ARP:
	case Family::ARP_HOST:
		return arp_field(*spec::menu_engine(ctx, song_of(where)).arp, row.path);
	case Family::AUDIO_CLIP: {
		AudioClipRig& a = audio_clip();
		const auto address = deluge::bridge::audioClipAddressOf(*a.song.clip, *a.song.song);
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(a.song.song->project,
		                                      {deluge::project::SettingGroupId::AUDIO_CLIP, *address}, row.path);
		break;
	}
	case Family::SONG:
		value = deluge::project::read_setting(
		    song_of(where).project, {deluge::project::SettingGroupId::SONG, deluge::project::SongAddress{}}, row.path);
		break;
	case Family::CLIP_VALUES: {
		Song& song = song_of(where);
		const auto address = deluge::bridge::clipAddressOf(
		    *static_cast<InstrumentClip*>(spec::menu_engine(ctx, song_of(where)).clip), song);
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(song.project, {deluge::project::SettingGroupId::CLIP_VALUES, *address},
		                                      row.path);
		break;
	}
	case Family::NOTE_ROW: {
		auto& clip = *static_cast<InstrumentClip*>(spec::menu_engine(ctx, song_of(where)).clip);
		NoteRow* note_row = clip.getNoteRowForDrum(kit_lane().drum);
		const auto address = deluge::bridge::noteRowAddressOf(*note_row, clip, song_of(where));
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(song_of(where).project,
		                                      {deluge::project::SettingGroupId::NOTE_ROW, *address}, row.path);
		break;
	}
	case Family::CLIP: {
		const auto address =
		    deluge::bridge::clipAddressOf(*spec::menu_engine(ctx, song_of(where)).clip, song_of(where));
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(song_of(where).project, {deluge::project::SettingGroupId::CLIP, *address},
		                                      row.path);
		break;
	}
	case Family::INSTRUMENT_DEVICE:
	case Family::AUDIO_INPUT: {
		const auto address =
		    deluge::bridge::trackAddressOf(*spec::menu_engine(ctx, song_of(where)).output, song_of(where));
		if (!address) {
			return std::nullopt;
		}
		const auto group = family == Family::AUDIO_INPUT ? deluge::project::SettingGroupId::AUDIO_INPUT
		                                                 : deluge::project::SettingGroupId::INSTRUMENT_DEVICE;
		value = deluge::project::read_setting(song_of(where).project, {group, *address}, row.path);
		break;
	}
	case Family::SOUND_ROUTING:
		value =
		    deluge::project::read_routing_setting(spec::menu_engine(ctx, song_of(where)).sound->routing(), row.path);
		break;
	}
	if (!value) {
		return std::nullopt;
	}
	return stored_form(row, *value);
}

// ---------------------------------------------------------------------------------------------
// The cases.

/// One row, where it is shown, and the values its checks probe.
struct Case {
	std::string_view path;
	Family family;
	Where where{Where::SYNTH};
	int8_t source{0};              ///< the oscillator the item edits
	std::vector<int32_t> stored{}; ///< stored values to read, beyond the row's stored edges and default
	std::vector<Range> pairs{};    ///< two-row kinds: {stored, partner stored} pairs to read
	bool writes{true};             ///< false where the item's write is not compared (a booted UI, a named exception)
	bool lossy{false};             ///< the record holds the value as a float: a set value reads back rounded
	std::vector<int32_t> skip_writes{}; ///< menu values whose item write is not compared (a named exception's)
	int32_t menu_step{0};               ///< nonzero: the item shows stored / menu_step and writes menu * menu_step
};

/// @brief An enumerator's name as written in its enum.
inline std::string_view name_of(Family family) {
	switch (family) {
	case Family::INSTRUMENT:
		return "INSTRUMENT";
	case Family::EFFECT:
		return "EFFECT";
	case Family::ARP:
		return "ARP";
	case Family::ARP_HOST:
		return "ARP_HOST";
	case Family::AUDIO_CLIP:
		return "AUDIO_CLIP";
	case Family::SONG:
		return "SONG";
	case Family::CLIP_VALUES:
		return "CLIP_VALUES";
	case Family::NOTE_ROW:
		return "NOTE_ROW";
	case Family::CLIP:
		return "CLIP";
	case Family::INSTRUMENT_DEVICE:
		return "INSTRUMENT_DEVICE";
	case Family::AUDIO_INPUT:
		return "AUDIO_INPUT";
	case Family::SOUND_ROUTING:
		return "SOUND_ROUTING";
	}
	return "";
}

/// @brief An enumerator's name as written in its enum.
inline std::string_view name_of(Where where) {
	switch (where) {
	case Where::SYNTH:
		return "SYNTH";
	case Where::SYNTH_SAMPLE:
		return "SYNTH_SAMPLE";
	case Where::SYNTH_FM:
		return "SYNTH_FM";
	case Where::SYNTH_RINGMOD:
		return "SYNTH_RINGMOD";
	case Where::KIT_LANE:
		return "KIT_LANE";
	case Where::KIT:
		return "KIT";
	case Where::KIT_GATE_ROW:
		return "KIT_GATE_ROW";
	case Where::MIDI_DRUM:
		return "MIDI_DRUM";
	case Where::GATE_DRUM:
		return "GATE_DRUM";
	case Where::MIDI_CLIP:
		return "MIDI_CLIP";
	case Where::CV_CLIP:
		return "CV_CLIP";
	case Where::AUDIO_CLIP:
		return "AUDIO_CLIP";
	case Where::SONG:
		return "SONG";
	}
	return "";
}

/// @brief A case's path in the reference tables: `case/<family>/<row path>@<where>#<source>`.
inline std::string case_key(const Case& c) {
	return "case/" + std::string{name_of(c.family)} + "/" + std::string{c.path} + "@" + std::string{name_of(c.where)}
	       + "#" + std::to_string(c.source);
}

/// @brief The reference rows of the setting descriptors.
inline spec::OracleTable& frozen() {
	return spec::OracleTable::named("descriptors");
}

/// @brief Value @p at of the row (@p path, @p column), or `<no row>` / `<no value>` so a miss prints as one.
inline std::string cell(std::string_view path, std::string_view column, size_t at = 0) {
	const std::vector<std::string>* values = frozen().find(path, column);
	if (values == nullptr) {
		return "<no row>";
	}
	return at < values->size() ? (*values)[at] : std::string{"<no value>"};
}

/// @brief The same, in the case @p c's own rows.
inline std::string cell(const Case& c, std::string_view column, size_t at = 0) {
	return cell(case_key(c), column, at);
}

/// @brief Value @p at of the row (@p path, @p column) as a number; nothing for a missing row or a value that is not
/// one.
inline std::optional<int32_t> cell_int(std::string_view path, std::string_view column, size_t at = 0) {
	const std::string text = cell(path, column, at);
	int32_t out = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return out;
}

/// How far a lossy case's value may read back from what it was set to: the float record's rounding step.
inline constexpr int32_t kLossyStep = 256;

/// Writes the reference rows hold none of: each reaches a UI global only a booted app has.
///   * `SynthModeSelection::writeCurrentValue` calls `view.setKnobIndicatorLevels()`;
///   * `arpeggiator::Mode` and `PresetMode` silence notes over the sound editor's model stack.
inline constexpr bool kNoFrozenWrite = false;
/// The volume ducking menu's step: the stored value is the menu value times it.
inline constexpr int32_t kDuckingVolumeStep = 21474836;

inline const SettingInfo& row_of(const Case& c) {
	const SettingInfo* row = deluge::plugin::find_setting(table_of(c.family), c.path);
	static const SettingInfo kMissing{.path = "<missing>"};
	return row != nullptr ? *row : kMissing;
}

/// @p row's partner row, or nullptr for a one-row kind.
inline const SettingInfo* partner_of(const Case& c, const SettingInfo& row) {
	const std::string_view partner = deluge::plugin::mapping_of(row).partner_path();
	return partner.empty() ? nullptr : deluge::plugin::find_setting(table_of(c.family), partner);
}

inline std::string show(const Case& c, std::string_view what, int64_t at, std::string_view value) {
	return std::string{c.path} + " " + std::string{what} + "(" + std::to_string(at) + ") = " + std::string{value};
}
inline std::string show(const Case& c, std::string_view what, int64_t at, int64_t value) {
	return show(c, what, at, std::to_string(value));
}

/// @brief A case's frozen menu range: the item's least and greatest value (`range`), or the sentinel where no row
///        holds one, which the range comparison then names.
inline Range frozen_range(const Case& c, std::string_view variant = {}) {
	const std::string column = "range" + std::string{variant};
	constexpr int32_t kNone = std::numeric_limits<int32_t>::min();
	return {cell_int(case_key(c), column, 0).value_or(kNone), cell_int(case_key(c), column, 1).value_or(kNone)};
}

/// The menu values to write: the edges, one in from each, and the middle.
inline std::vector<int32_t> menu_probes(Range range) {
	std::vector<int32_t> probes{range.first, range.second};
	if (range.second > range.first) {
		probes.push_back(range.first + 1);
		probes.push_back(range.second - 1);
		probes.push_back(static_cast<int32_t>((int64_t{range.first} + range.second) / 2));
	}
	return probes;
}

/// Each numeric case: its range, its reads at the stored probes and its writes at the menu edges, against the item's
/// rows. @p variant is the column suffix of a state the rows were also taken in (`@plugged`).
inline void check_numeric(auto& self, const Case& c, std::string_view variant = {}) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	const auto column = [&](const std::string& base) { return base + std::string{variant}; };
	expect(std::string{c.path} + " has a row").to_equal(std::string{row.path} + " has a row");
	const Range range = menu_range(row);
	expect(std::string{c.path} + " range " + std::to_string(range.first) + ".." + std::to_string(range.second))
	    .to_equal(std::string{c.path} + " range " + cell(key, column("range"), 0) + ".."
	              + cell(key, column("range"), 1));

	const SettingInfo* partner = partner_of(c, row);
	if (partner == nullptr) {
		const auto [least, greatest] = deluge::plugin::stored_bounds(row);
		std::vector<int32_t> probes{least, greatest, row.default_value};
		probes.insert(probes.end(), c.stored.begin(), c.stored.end());
		for (int32_t stored : probes) {
			set_stored(c.family, ctx, c.where, row, stored);
			const std::optional<int32_t> now = get_stored(c.family, ctx, c.where, row);
			const int32_t held = c.lossy ? now.value_or(-99999) : stored;
			if (c.lossy) {
				// The record's float rounds what it was set to: the value read back is within one float step of it.
				expect(held).to_be_within(kLossyStep, show(c, "set", stored, held)).of(stored);
			}
			else {
				expect(show(c, "set", stored, now.value_or(-99999))).to_equal(show(c, "set", stored, held));
			}
			expect(show(c, "read", held, to_menu(row, held)))
			    .to_equal(show(c, "read", held, cell(key, column("read/" + std::to_string(held)))));
		}
	}
	else {
		for (const auto& [stored, partner_stored] : c.pairs) {
			set_stored(c.family, ctx, c.where, row, stored);
			set_stored(c.family, ctx, c.where, *partner, partner_stored);
			const int64_t at = int64_t{stored} * 1000 + partner_stored;
			expect(show(c, "read", at, to_menu(row, stored, partner_stored)))
			    .to_equal(show(c, "read", at, cell(key, column("read/" + std::to_string(at)))));
		}
	}

	if (!c.writes) {
		return;
	}
	// The item's own range bounds its writes: a value past it indexes past its tables.
	for (int32_t menu : menu_probes(frozen_range(c, variant))) {
		if (std::ranges::find(c.skip_writes, menu) != c.skip_writes.end()) {
			continue;
		}
		const std::optional<std::pair<int32_t, int32_t>> want = from_menu(row, menu);
		int64_t expected = want ? want->first : -12345;
		if (c.lossy && want) {
			// The record rounds what either write stores alike: compare what it holds after each.
			set_stored(c.family, ctx, c.where, row, want->first);
			expected = get_stored(c.family, ctx, c.where, row).value_or(-12345);
		}
		const std::string at = column("write/" + std::to_string(menu));
		expect(show(c, "write", menu, expected)).to_equal(show(c, "write", menu, cell(key, at)));
		if (partner != nullptr) {
			expect(show(c, "write partner", menu, want ? want->second : -12345))
			    .to_equal(show(c, "write partner", menu, cell(key, at, 1)));
		}
	}
}

/// The table a @p family row's conditions read: an arp's host rows read its setting rows too.
inline std::span<const SettingInfo> condition_table_of(Family family) {
	return family == Family::ARP || family == Family::ARP_HOST ? deluge::plugin::kArpRows : table_of(family);
}

/// The owner @p ctx edits, read by path as a firmware reader reads it: the value the owner holds whether or not the
/// row applies (a sound's oscillator type in FM included), through the spec's own read path for the family. A path
/// the family's table does not hold is recorded, and `applicable` fails on it rather than reading 0.
struct SpecReader {
	const MenuContext& ctx;
	Where where;
	Family family{Family::INSTRUMENT};
	mutable std::vector<std::string> unknown{};

	[[nodiscard]] deluge::plugin::SettingReader reader() const {
		return {.read = [](const void* self, std::string_view path) -> std::optional<int32_t> {
			        const auto& owner = *static_cast<const SpecReader*>(self);
			        const SettingInfo* row = deluge::plugin::find_setting(condition_table_of(owner.family), path);
			        if (row == nullptr) {
				        owner.unknown.emplace_back(path);
				        return std::nullopt;
			        }
			        switch (owner.family) {
			        case Family::INSTRUMENT: {
				        const std::optional<int32_t> held = deluge::instruments::classic::held_setting(
				            spec::menu_engine(owner.ctx, song_of(owner.where)).sound->state(), path);
				        if (!held) {
					        owner.unknown.emplace_back(path);
				        }
				        return held;
			        }
			        case Family::ARP:
			        case Family::ARP_HOST:
				        return arp_field(*spec::menu_engine(owner.ctx, song_of(owner.where)).arp, path);
			        default:
				        return get_stored(owner.family, owner.ctx, owner.where, *row);
			        }
		        },
		        .self = this};
	}
};

/// The choices of @p row that apply in @p shown on the owner @p ctx edits, in order, through the engine's own position
/// helpers. Throws where a condition read a path the owner does not hold, or a position does not round-trip.
inline std::vector<int32_t> applicable(const SettingInfo& row, const SettingContext& shown, const MenuContext& ctx,
                                       Where where) {
	if (!deluge::plugin::context_is_coherent(shown)) {
		throw std::logic_error(std::string{row.path} + ": a setting context whose owner and instrument do not pair");
	}
	const SpecReader owner{ctx, where};
	const deluge::plugin::SettingReader reader = owner.reader();
	std::vector<int32_t> out;
	const int32_t count = deluge::plugin::applicable_choice_count(row, shown, reader);
	for (int32_t position = 0; position < count; ++position) {
		const std::optional<int32_t> stored = deluge::plugin::choice_at_position(row, shown, reader, position);
		if (!stored || deluge::plugin::choice_position(row, shown, reader, *stored) != position) {
			throw std::runtime_error(std::string{row.path} + ": choice position " + std::to_string(position)
			                         + " does not round-trip");
		}
		out.push_back(*stored);
	}
	if (!owner.unknown.empty()) {
		throw std::runtime_error(std::string{row.path} + " reads " + owner.unknown.front()
		                         + ", which its owner does not hold");
	}
	return out;
}

/// The same, on the owner @p where names, set up as `enter` leaves it.
inline std::vector<int32_t> applicable(const SettingInfo& row, Where where, Family family) {
	return applicable(row, context_of(where, family), enter(where), where);
}

inline std::string list_of(const std::vector<int32_t>& choices) {
	std::string out;
	for (int32_t choice : choices) {
		out += std::to_string(choice) + ",";
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// The host's names for a row.

using deluge::l10n::NamesOwner;
using deluge::l10n::SettingNames;
using deluge::project::SettingGroupId;
namespace plugin = deluge::plugin;

/// @brief The owners whose table a family's rows are in.
inline std::vector<NamesOwner> owners_of(Family family) {
	switch (family) {
	case Family::INSTRUMENT:
		return {plugin::kDelugeInstrument};
	case Family::EFFECT:
		return {plugin::kDelugeInstrument, plugin::kGlobalEffectable};
	case Family::ARP:
		return {plugin::kDelugeInstrument, SettingGroupId::CLIP_ARP, SettingGroupId::LANE_ARP};
	case Family::ARP_HOST:
		return {SettingGroupId::CLIP_ARP, SettingGroupId::LANE_ARP};
	case Family::AUDIO_CLIP:
		return {SettingGroupId::AUDIO_CLIP};
	case Family::SONG:
		return {SettingGroupId::SONG};
	case Family::CLIP_VALUES:
		return {SettingGroupId::CLIP_VALUES};
	case Family::NOTE_ROW:
		return {SettingGroupId::NOTE_ROW};
	case Family::CLIP:
		return {SettingGroupId::CLIP};
	case Family::INSTRUMENT_DEVICE:
		return {SettingGroupId::INSTRUMENT_DEVICE};
	case Family::AUDIO_INPUT:
		return {SettingGroupId::AUDIO_INPUT};
	case Family::SOUND_ROUTING:
		return {SettingGroupId::SOUND_ROUTING};
	}
	return {};
}

inline std::string text(deluge::l10n::String id) {
	return std::string{deluge::l10n::getView(id)};
}

/// @brief A title with its `*` replaced by the owner's number, which the path's first digit gives as legacy's
/// formatted titles do.
inline std::string title_of(const SettingNames& names, std::string_view path) {
	std::string title = text(names.title);
	const size_t star = title.find('*');
	const size_t digit = path.find_first_of("0123456789");
	if (star != std::string::npos && digit != std::string_view::npos) {
		title[star] = path[digit];
	}
	return title;
}

/// @brief The names table's text for choice @p index, full or short (short falls back to full where there are none).
/// @param on_kit_lane A kit lane's arpeggiator: legacy's drum items there (`NoteModeForDrums`) keep their options' full
///                    names in a column, so a row that shortens through `shortLabel` elsewhere does not here.
inline std::string choice_text(const SettingNames& names, size_t index, bool short_form, bool on_kit_lane = false) {
	if (short_form && names.short_from_labels && !on_kit_lane) {
		return index < names.choices.size() ? std::string{deluge::l10n::shortLabel(names.choices[index])}
		                                    : std::string{"<no choice>"};
	}
	const auto list = short_form && !names.short_choices.empty() ? names.short_choices : names.choices;
	return index < list.size() ? text(list[index]) : std::string{"<no choice>"};
}

/// A CHOICE case in its context: the list's length is the options' count, each applicable choice reads as its
/// position in the list, and each position writes that choice.
inline void check_choices(auto& self, const Case& c, std::string_view variant = {}) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	const auto column = [&](const std::string& base) { return base + std::string{variant}; };
	const std::vector<int32_t> choices = applicable(row, context_of(c.where, c.family), ctx, c.where);
	const Range frozen_items = frozen_range(c, variant);
	const std::string listed = cell_int(key, column("range"), 1) ? std::to_string(int64_t{frozen_items.second} + 1)
	                                                             : cell(key, column("range"), 1);
	expect(std::string{c.path} + " offers " + std::to_string(choices.size()) + ": " + list_of(choices))
	    .to_equal(std::string{c.path} + " offers " + listed + ": " + list_of(choices));
	for (size_t position = 0; position < choices.size(); ++position) {
		set_stored(c.family, ctx, c.where, row, choices[position]);
		expect(show(c, "read", choices[position], static_cast<int64_t>(position)))
		    .to_equal(
		        show(c, "read", choices[position], cell(key, column("read/" + std::to_string(choices[position])))));
	}
	if (!c.writes) {
		return;
	}
	for (size_t position = 0; position < choices.size() && static_cast<int32_t>(position) <= frozen_items.second;
	     ++position) {
		expect(show(c, "write", static_cast<int64_t>(position), choices[position]))
		    .to_equal(show(c, "write", static_cast<int64_t>(position),
		                   cell(key, column("write/" + std::to_string(position)))));
	}
}

/// A case whose item shows the stored value in steps of `menu_step`, truncating: its reads and writes are compared
/// with that, as the row's menu is the stored value itself (`reverb_volume_differs`).
inline void check_scaled(auto& self, const Case& c) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	expect(std::string{c.path} + " has a row").to_equal(std::string{row.path} + " has a row");
	const int32_t step = c.menu_step;
	std::vector<int32_t> probes{-step, -1, 0, step - 1, step, 5 * step + 1000, 50 * step, 51 * step - 1};
	probes.insert(probes.end(), c.stored.begin(), c.stored.end());
	for (const int32_t stored : probes) {
		set_stored(c.family, ctx, c.where, row, stored);
		expect(show(c, "read", stored, stored / step))
		    .to_equal(show(c, "read", stored, cell(key, "read/" + std::to_string(stored))));
	}
	for (const int32_t menu : menu_probes(frozen_range(c))) {
		expect(show(c, "write", menu, int64_t{menu} * step))
		    .to_equal(show(c, "write", menu, cell(key, "write/" + std::to_string(menu))));
	}
}

inline void check(auto& self, const Case& c, std::string_view variant = {}) {
	const SettingInfo& row = row_of(c);
	if (row.kind == SettingKind::CHOICE
	    && deluge::plugin::mapping_of(row).kind == deluge::plugin::DisplayMappingKind::LINEAR) {
		check_choices(self, c, variant);
	}
	else {
		check_numeric(self, c, variant);
	}
}

inline constexpr int32_t kQ31Max = std::numeric_limits<int32_t>::max();
inline constexpr int32_t kQ31Min = std::numeric_limits<int32_t>::min();

/// The sync pairs worth reading: {level, type choice}.
inline const std::vector<Range> kSyncPairs{{0, 0}, {1, 0}, {9, 0}, {1, 1}, {9, 1}, {1, 2}, {9, 2}};
/// The transpose pairs worth reading: {semitones, cents}.
inline const std::vector<Range> kTransposePairs{{0, 0}, {-96, 0}, {96, 0}, {2, -50}, {-1, 49}, {1, -50}, {0, 50}};
/// Retrigger phases worth reading: free, the degree boundaries, 180 and 360.
inline const std::vector<int32_t> kPhases{-1, 0, 11930463, 11930464, 2147483520, -11930720, -256};

// ---------------------------------------------------------------------------------------------
// Each family's cases.

inline const std::vector<Case>& voice_cases() {
	static const std::vector<Case> cases{
	    {.path = "polyphonic", .family = Family::INSTRUMENT},
	    {.path = "polyphonic", .family = Family::INSTRUMENT, .where = Where::KIT_LANE},
	    {.path = "voicePriority", .family = Family::INSTRUMENT},
	    {.path = "maxVoices", .family = Family::INSTRUMENT, .stored = {1, 16, 17, 126, 127}},
	    {.path = "sideChainSend",
	     .family = Family::SOUND_ROUTING,
	     .where = Where::KIT_LANE,
	     .stored = {21474836, 21474837, 42949673, 1073741823, 2126008811, 2126008812}},
	    {.path = "mode", .family = Family::INSTRUMENT, .writes = kNoFrozenWrite},
	    {.path = "transpose", .family = Family::INSTRUMENT, .pairs = kTransposePairs},
	};
	return cases;
}

inline const std::vector<Case>& oscillator_cases() {
	static const std::vector<Case> cases{
	    {.path = "osc1/type", .family = Family::INSTRUMENT},
	    {.path = "osc2/type", .family = Family::INSTRUMENT, .source = 1},
	    {.path = "osc1/type", .family = Family::INSTRUMENT, .where = Where::SYNTH_RINGMOD},
	    {.path = "osc1/type", .family = Family::INSTRUMENT, .where = Where::KIT_LANE},
	    {.path = "osc1/transpose", .family = Family::INSTRUMENT, .pairs = kTransposePairs},
	    {.path = "osc2/transpose", .family = Family::INSTRUMENT, .source = 1, .pairs = kTransposePairs},
	    {.path = "osc1/retrigPhase", .family = Family::INSTRUMENT, .stored = kPhases},
	    {.path = "osc2/retrigPhase", .family = Family::INSTRUMENT, .source = 1, .stored = kPhases},
	    {.path = "osc1/loopMode", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/loopMode", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE, .source = 1},
	    {.path = "osc1/reversed", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/reversed", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE, .source = 1},
	    {.path = "osc1/timeStretchEnable", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/timeStretchEnable", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE, .source = 1},
	    {.path = "osc1/timeStretchAmount", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/timeStretchAmount", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE, .source = 1},
	    {.path = "osc1/linearInterpolation", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/linearInterpolation", .family = Family::INSTRUMENT, .where = Where::SYNTH_SAMPLE, .source = 1},
	    {.path = "osc2/oscillatorSync", .family = Family::INSTRUMENT},
	};
	return cases;
}

inline const std::vector<Case>& lfo_and_modulator_cases() {
	static const std::vector<Case> cases{
	    {.path = "lfo1/type", .family = Family::INSTRUMENT},
	    {.path = "lfo2/type", .family = Family::INSTRUMENT},
	    {.path = "lfo3/type", .family = Family::INSTRUMENT},
	    {.path = "lfo4/type", .family = Family::INSTRUMENT},
	    {.path = "lfo1/syncLevel", .family = Family::INSTRUMENT, .pairs = kSyncPairs},
	    {.path = "lfo2/syncLevel", .family = Family::INSTRUMENT, .pairs = kSyncPairs},
	    {.path = "lfo3/syncLevel", .family = Family::INSTRUMENT, .pairs = kSyncPairs},
	    {.path = "lfo4/syncLevel", .family = Family::INSTRUMENT, .pairs = kSyncPairs},
	    {.path = "envelope1Settings/mode", .family = Family::INSTRUMENT},
	    {.path = "envelope1Settings/curve", .family = Family::INSTRUMENT},
	    {.path = "envelope1Settings/follow", .family = Family::INSTRUMENT},
	    {.path = "envelope2Settings/mode", .family = Family::INSTRUMENT},
	    {.path = "envelope2Settings/curve", .family = Family::INSTRUMENT},
	    {.path = "envelope2Settings/follow", .family = Family::INSTRUMENT},
	    {.path = "envelope3Settings/mode", .family = Family::INSTRUMENT},
	    {.path = "envelope3Settings/curve", .family = Family::INSTRUMENT},
	    {.path = "envelope3Settings/follow", .family = Family::INSTRUMENT},
	    {.path = "envelope4Settings/mode", .family = Family::INSTRUMENT},
	    {.path = "envelope4Settings/curve", .family = Family::INSTRUMENT},
	    {.path = "envelope4Settings/follow", .family = Family::INSTRUMENT},
	    {.path = "modulator1/transpose",
	     .family = Family::INSTRUMENT,
	     .where = Where::SYNTH_FM,
	     .pairs = kTransposePairs},
	    {.path = "modulator2/transpose",
	     .family = Family::INSTRUMENT,
	     .where = Where::SYNTH_FM,
	     .source = 1,
	     .pairs = kTransposePairs},
	    {.path = "modulator1/retrigPhase", .family = Family::INSTRUMENT, .where = Where::SYNTH_FM, .stored = kPhases},
	    {.path = "modulator2/retrigPhase",
	     .family = Family::INSTRUMENT,
	     .where = Where::SYNTH_FM,
	     .source = 1,
	     .stored = kPhases},
	    {.path = "modulator2/toModulator1", .family = Family::INSTRUMENT, .where = Where::SYNTH_FM},
	};
	return cases;
}

inline const std::vector<Case>& unison_and_midi_cases() {
	static const std::vector<Case> cases{
	    {.path = "unison/num", .family = Family::INSTRUMENT},
	    {.path = "unison/detune", .family = Family::INSTRUMENT},
	    {.path = "unison/spread", .family = Family::INSTRUMENT},
	    {.path = "midiOutput/channel", .family = Family::SOUND_ROUTING},
	    {.path = "midiOutput/noteForDrum", .family = Family::SOUND_ROUTING, .where = Where::KIT_LANE},
	};
	return cases;
}

inline const std::vector<Case>& effect_cases() {
	static const std::vector<Case> cases{
	    {.path = "modFXType", .family = Family::EFFECT},
	    {.path = "lpfMode", .family = Family::EFFECT},
	    {.path = "hpfMode", .family = Family::EFFECT},
	    {.path = "filterRoute", .family = Family::EFFECT},
	    {.path = "clippingAmount", .family = Family::EFFECT},
	    {.path = "delay/pingPong", .family = Family::EFFECT},
	    {.path = "delay/type", .family = Family::EFFECT},
	    {.path = "delay/syncLevel", .family = Family::EFFECT, .pairs = kSyncPairs},
	    {.path = "sidechain/attack", .family = Family::EFFECT, .stored = {327244, 1048576, 256, 255}},
	    {.path = "sidechain/release", .family = Family::EFFECT, .stored = {936, 261528, 256, 264}},
	    {.path = "sidechain/syncLevel", .family = Family::EFFECT, .pairs = {{0, 0}, {1, 0}, {9, 0}, {4, 0}}},
	    {.path = "audioCompressor/attack",
	     .family = Family::EFFECT,
	     .stored = {5 << 24, (5 << 24) + 0xFFFFFF, 127 << 24}},
	    {.path = "audioCompressor/release", .family = Family::EFFECT, .stored = {5 << 24, 127 << 24}},
	    {.path = "audioCompressor/ratio", .family = Family::EFFECT, .stored = {64 << 24, 1}},
	    {.path = "audioCompressor/compHPF", .family = Family::EFFECT, .stored = {(3 << 24) - 1}},
	    {.path = "audioCompressor/compBlend", .family = Family::EFFECT, .stored = {127 << 24}},
	    {.path = "stutter/quantized", .family = Family::EFFECT},
	};
	return cases;
}

inline const std::vector<Case>& arp_cases() {
	static const std::vector<Case> cases{
	    {.path = "arpeggiator/arpMode", .family = Family::ARP, .writes = kNoFrozenWrite},
	    {.path = "arpeggiator/octaveMode", .family = Family::ARP},
	    {.path = "arpeggiator/noteMode", .family = Family::ARP},
	    {.path = "arpeggiator/noteMode", .family = Family::ARP, .where = Where::KIT_LANE},
	    {.path = "arpeggiator/mpeVelocity", .family = Family::ARP},
	    {.path = "arpeggiator/numOctaves", .family = Family::ARP},
	    {.path = "arpeggiator/stepRepeat", .family = Family::ARP},
	    {.path = "arpeggiator/chordType", .family = Family::ARP, .where = Where::KIT_LANE},
	    {.path = "arpeggiator/syncLevel", .family = Family::ARP, .pairs = kSyncPairs},
	    {.path = "arpeggiator/randomizerLock", .family = Family::ARP},
	    {.path = "arpeggiator/kitArp", .family = Family::ARP, .where = Where::KIT_LANE},
	    {.path = "arpeggiator/preset", .family = Family::ARP, .writes = kNoFrozenWrite},
	};
	return cases;
}

/// The host rows' standard and unsigned values worth reading.
inline const std::vector<int32_t> kQ31Probes{0, -1, 1, 85899345, 42949672, 42949673, kQ31Min + 42949672};

inline const std::vector<Case>& arp_host_cases() {
	static const std::vector<Case> cases = [] {
		static constexpr std::string_view paths[] = {
		    "arpeggiator/gate",
		    "arpeggiator/rate",
		    "arpeggiator/rhythm",
		    "arpeggiator/sequenceLength",
		    "arpeggiator/chordPolyphony",
		    "arpeggiator/ratchetAmount",
		    "arpeggiator/noteProbability",
		    "arpeggiator/bassProbability",
		    "arpeggiator/swapProbability",
		    "arpeggiator/glideProbability",
		    "arpeggiator/chordProbability",
		    "arpeggiator/ratchetProbability",
		    "arpeggiator/spreadVelocity",
		    "arpeggiator/spreadGate",
		    "arpeggiator/spreadOctave",
		};
		std::vector<Case> out;
		for (const std::string_view path : paths) {
			out.push_back({.path = path, .family = Family::ARP_HOST, .where = Where::MIDI_CLIP, .stored = kQ31Probes});
		}
		return out;
	}();
	return cases;
}

/// Q31 fractions either side of each rounding edge a fiftieths menu has, (k - 1/2) / 50 of 2^31, for a few k.
inline const std::vector<int32_t> kFractionProbes = [] {
	std::vector<int32_t> out;
	for (int32_t k : {1, 13, 25, 37, 49, 50}) {
		const int64_t edge = ((2 * int64_t{k} - 1) << 31) / 100;
		for (int64_t d : {-256, -128, -1, 0, 1, 128, 256}) {
			out.push_back(static_cast<int32_t>(edge + d));
		}
	}
	return out;
}();

inline const std::vector<Case>& model_cases() {
	static const std::vector<Case> cases{
	    // Driven as a numeric row against legacy's DualCVSelection: stored 1 (pitch), a mode the menu does not list,
	    // reads as off.
	    {.path = "cv2Source", .family = Family::INSTRUMENT_DEVICE, .where = Where::CV_CLIP, .stored = {1, 2, 3}},
	    {.path = "transpose", .family = Family::AUDIO_CLIP, .where = Where::AUDIO_CLIP, .pairs = kTransposePairs},
	    {.path = "linearInterpolation", .family = Family::AUDIO_CLIP, .where = Where::AUDIO_CLIP},
	    {.path = "reverb/roomSize",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/dampening",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/width",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/hpf", .family = Family::SONG, .where = Where::SONG, .stored = kFractionProbes, .lossy = true},
	    {.path = "reverb/lpf", .family = Family::SONG, .where = Where::SONG, .stored = kFractionProbes, .lossy = true},
	    {.path = "reverb/pan", .family = Family::SONG, .where = Where::SONG, .stored = {0, 1, -1, 85899344}},
	    {.path = "reverb/model", .family = Family::SONG, .where = Where::SONG},
	    {.path = "reverb/compressor/shape",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .stored = {0, -601295438, 85899345},
	     .skip_writes = {50}},
	    {.path = "swingInterval", .family = Family::SONG, .where = Where::SONG},
	    {.path = "priority", .family = Family::AUDIO_CLIP, .where = Where::AUDIO_CLIP},
	    {.path = "reversed", .family = Family::AUDIO_CLIP, .where = Where::AUDIO_CLIP},
	    {.path = "pitchSpeedIndependent", .family = Family::AUDIO_CLIP, .where = Where::AUDIO_CLIP},
	    {.path = "attack", .family = Family::AUDIO_CLIP, .where = Where::AUDIO_CLIP, .stored = {0, -1, 1, 85899345}},
	    {.path = "midiBank", .family = Family::CLIP_VALUES, .where = Where::MIDI_CLIP},
	    {.path = "midiSub", .family = Family::CLIP_VALUES, .where = Where::MIDI_CLIP},
	    {.path = "midiPGM", .family = Family::CLIP_VALUES, .where = Where::MIDI_CLIP},
	    {.path = "bendRange", .family = Family::CLIP_VALUES},
	    {.path = "bendRangeMPE", .family = Family::CLIP_VALUES},
	    {.path = "reverb/compressor/attack",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .stored = {327244, 1048576, 256, 255}},
	    {.path = "reverb/compressor/release",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .stored = {936, 261528, 256, 264}},
	};
	return cases;
}

// ---------------------------------------------------------------------------------------------
// The named skips: rows with a menu but no single legacy item to compare, and why.

/// The owners of the skipped rows: a skip names the tables it is a row of.
inline constexpr NamesOwner kInstrumentOwner[] = {plugin::kDelugeInstrument};
inline constexpr NamesOwner kEffectOwners[] = {plugin::kDelugeInstrument, plugin::kGlobalEffectable};
inline constexpr NamesOwner kArpOwners[] = {plugin::kDelugeInstrument, SettingGroupId::CLIP_ARP,
                                            SettingGroupId::LANE_ARP};
inline constexpr NamesOwner kHostArpOwners[] = {SettingGroupId::CLIP_ARP, SettingGroupId::LANE_ARP};
inline constexpr NamesOwner kSongOwner[] = {SettingGroupId::SONG};
inline constexpr NamesOwner kAudioClipOwner[] = {SettingGroupId::AUDIO_CLIP};

struct Skip {
	std::span<const NamesOwner> owners; ///< the owners whose table holds the row
	std::string_view path;
	std::string_view reason;
};

inline constexpr Skip kSkips[] = {
    {kInstrumentOwner, "cents", "the master transpose's partner: shown only within `transpose`'s menu value"},
    {kAudioClipOwner, "cents", "the audio clip transpose's partner: shown only within `transpose`'s menu value"},
    {kInstrumentOwner, "osc1/cents", "osc 1 transpose's partner: shown only within its menu value"},
    {kInstrumentOwner, "osc2/cents", "osc 2 transpose's partner: shown only within its menu value"},
    {kInstrumentOwner, "modulator1/cents", "modulator 1 transpose's partner: shown only within its menu value"},
    {kInstrumentOwner, "modulator2/cents", "modulator 2 transpose's partner: shown only within its menu value"},
    {kInstrumentOwner, "lfo1/syncType", "LFO 1 sync's partner: shown only within its combined sync menu"},
    {kInstrumentOwner, "lfo2/syncType", "LFO 2 sync's partner: shown only within its combined sync menu"},
    {kInstrumentOwner, "lfo3/syncType", "LFO 3 sync's partner: shown only within its combined sync menu"},
    {kInstrumentOwner, "lfo4/syncType", "LFO 4 sync's partner: shown only within its combined sync menu"},
    {kEffectOwners, "delay/syncType", "the delay sync's partner: shown only within its combined sync menu"},
    {kEffectOwners, "sidechain/syncType", "the sidechain sync's partner: shown only within its combined sync menu"},
    {kArpOwners, "arpeggiator/syncType", "the arp sync's partner: shown only within its combined sync menu"},
    {kInstrumentOwner, "osc1/sampleRanges", "structured: the sample browser and range editors, no value menu"},
    {kInstrumentOwner, "osc2/sampleRanges", "structured: the sample browser and range editors, no value menu"},
    {kInstrumentOwner, "osc1/wavetableRanges", "structured: the wavetable browser, no value menu"},
    {kInstrumentOwner, "osc2/wavetableRanges", "structured: the wavetable browser, no value menu"},
    {kInstrumentOwner, "osc1/fileName", "structured: the sample browser, no value menu"},
    {kInstrumentOwner, "osc2/fileName", "structured: the sample browser, no value menu"},
    {kInstrumentOwner, "osc1/zone", "structured: the sample marker editor, no value menu"},
    {kInstrumentOwner, "osc2/zone", "structured: the sample marker editor, no value menu"},
    {kInstrumentOwner, "osc1/dx7patch", "structured: the DX7 editor, no value menu"},
    {kInstrumentOwner, "osc2/dx7patch", "structured: the DX7 editor, no value menu"},
    {kInstrumentOwner, "osc1/dx7enginemode", "structured: the DX7 editor, no value menu"},
    {kInstrumentOwner, "osc2/dx7enginemode", "structured: the DX7 editor, no value menu"},
    {kInstrumentOwner, "osc1/dx7randomdetune", "structured: the DX7 editor, no value menu"},
    {kInstrumentOwner, "osc2/dx7randomdetune", "structured: the DX7 editor, no value menu"},
    {kEffectOwners, "stutter/reverse",
     "one of three rows `stutter::StutterDirection` shows as one list (with pingPong, useSongStutter)"},
    {kEffectOwners, "stutter/pingPong",
     "one of three rows `stutter::StutterDirection` shows as one list (with reverse, useSongStutter)"},
    {kEffectOwners, "stutter/useSongStutter",
     "one of three rows `stutter::StutterDirection` shows as one list (with reverse, pingPong); its first entry "
     "appears only off the song"},
    {kEffectOwners, "audioCompressor/thresh",
     "no menu item: a sound's threshold is a param; the knob gesture writes the setting"},
    {kHostArpOwners, "arpeggiator/reverseProbability",
     "no MIDI or CV item: the reverse probability is a param-only menu"},
    {kSongOwner, "reverb/compressor/syncLevel",
     "its menu shows one value over this level and the engine's own sync type, which no row of the song holds"},
    {kSongOwner, "reverb/compressor/volume", "a named exception: menu -1..50 truncates (stored / 21474836)"},
    {kSongOwner, "syncScalingClip", "no menu: the clip whose length scales the tempo, set by a gesture"},
    {kSongOwner, "scales/disabled/", "every preset scale's row: one active-scales menu shows them all"},
    {kAudioClipOwner, "startSamplePos", "the sample marker editor, no value menu"},
    {kAudioClipOwner, "endSamplePos", "the sample marker editor, no value menu"},
    {kAudioClipOwner, "overdubsShouldCloneAudioTrack", "no menu: a recording gesture sets it"},
};

/// The cases whose item shows the stored value in steps (`Case::menu_step`): compared by `check_scaled` against the
/// item's own truncating step, away from the points the exception for the row names.
inline const std::vector<Case>& scaled_cases() {
	static const std::vector<Case> cases{
	    {.path = "reverb/compressor/volume",
	     .family = Family::SONG,
	     .where = Where::SONG,
	     .menu_step = kDuckingVolumeStep},
	};
	return cases;
}

/// A row whose legacy item differs from the row's menu, the difference, and a probe that measures it against the item.
struct Exception {
	NamesOwner owner; ///< the owner whose table holds the row
	std::string_view path;
	std::string_view reason;
	bool (*differs)(); ///< true while the row's menu and the item still differ as `reason` says
};

inline const SettingInfo& table_row(Family family, std::string_view path) {
	const SettingInfo* row = deluge::plugin::find_setting(table_of(family), path);
	static const SettingInfo kMissing{.path = "<missing>"};
	return row != nullptr ? *row : kMissing;
}

/// @brief The frozen readings of the exception for @p path: `exception/<path>`, `probe`.
inline std::string probe_cell(std::string_view path, size_t at = 0) {
	return cell("exception/" + std::string{path}, "probe", at);
}
inline std::optional<int32_t> probe_int(std::string_view path, size_t at = 0) {
	return cell_int("exception/" + std::string{path}, "probe", at);
}

/// An unset note (stored 255, which no menu value of the row stores) shows as `to_menu(row, 255)`, the note it plays;
/// the item's reading of it is the frozen probe.
inline bool note_for_drum_differs() {
	const SettingInfo& row = table_row(Family::SOUND_ROUTING, "midiOutput/noteForDrum");
	const std::optional<int32_t> shown = probe_int("midiOutput/noteForDrum");
	const int32_t unset = int32_t{MIDI_NOTE_NONE};
	return shown && unset > deluge::plugin::stored_bounds(row).second && *shown == to_menu(row, unset);
}

/// With no input plugged in the item lists one input, so a stored right or stereo input reads past its list: the
/// item's two readings (right, then stereo) are the frozen probes.
inline bool input_reads_past_list(std::string_view path, int8_t source) {
	const bool mic = AudioEngine::micPluggedIn;
	const bool line_in = AudioEngine::lineInPluggedIn;
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	enter(Where::SYNTH, source);
	const SettingInfo& row = table_row(Family::INSTRUMENT, path);
	const auto listed = static_cast<int32_t>(applicable(row, Where::SYNTH, Family::INSTRUMENT).size());
	bool past = true;
	for (const size_t input : {size_t{0}, size_t{1}}) {
		const std::optional<int32_t> read = probe_int(path, input);
		past = past && read && *read >= listed;
	}
	AudioEngine::micPluggedIn = mic;
	AudioEngine::lineInPluggedIn = line_in;
	return past;
}
inline bool first_oscillator_type_differs() {
	return input_reads_past_list("osc1/type", 0);
}
inline bool second_oscillator_type_differs() {
	return input_reads_past_list("osc2/type", 1);
}

/// The item's write of its top step stores a different value from the row's. The probe holds the step it wrote, which
/// must be the row's top, and what the item stored (`none` where the record held nothing).
inline bool compressor_shape_differs() {
	const SettingInfo& row = table_row(Family::SONG, "reverb/compressor/shape");
	const int32_t top = menu_range(row).second;
	if (probe_int("reverb/compressor/shape", 0) != top) {
		return false;
	}
	const std::optional<std::pair<int32_t, int32_t>> want = from_menu(row, top);
	const std::optional<int32_t> stored = probe_int("reverb/compressor/shape", 1);
	return want && want->first != stored.value_or(want->first);
}

/// The item's list holds 0, which its encoder never lands on: `wrapSwingIntervalSyncLevel` is the encoder's path, and
/// stepping down from 1 lands elsewhere than 0. The probe is the item's least value.
inline bool swing_interval_differs() {
	const SettingInfo& row = table_row(Family::SONG, "swingInterval");
	return probe_int("swingInterval") == 0 && menu_range(row).first == 0 && wrapSwingIntervalSyncLevel(1 - 1) != 0;
}

/// The item shows the stored volume as stored / kDuckingVolumeStep, truncating (5 steps and 1000 shows 5); the row's
/// menu is the stored value itself. The probe is what the item showed.
inline bool reverb_volume_differs() {
	const SettingInfo& row = table_row(Family::SONG, "reverb/compressor/volume");
	const int32_t stored = 5 * kDuckingVolumeStep + 1000;
	return probe_int("reverb/compressor/volume") == 5 && to_menu(row, stored) == stored;
}

/// The rows a legacy item shows but this spec reads only around a named difference. Each is checked to still differ
/// (`descriptor_oracle_spec.cpp`), so a row that comes to agree leaves the list.
inline const Exception kExceptions[] = {
    {SettingGroupId::SOUND_ROUTING, "midiOutput/noteForDrum",
     "unset (stored 255, outside the row) shows kUnsetDrumNoteShownAs (60) through CLAMP_HIGH; no menu value stores "
     "it",
     &note_for_drum_differs},
    {plugin::kDelugeInstrument, "osc1/type",
     "with no input plugged in, the one input entry is INPUT_L under the name \"INPUT\"; a stored INPUT_R or "
     "INPUT_STEREO then reads past the list",
     &first_oscillator_type_differs},
    {plugin::kDelugeInstrument, "osc2/type", "as osc1/type", &second_oscillator_type_differs},
    {SettingGroupId::SONG, "reverb/compressor/shape",
     "the read is STANDARD; legacy's write omits the top step's special case (menu 50 stores 2147483602, not "
     "2147483647)",
     &compressor_shape_differs},
    {SettingGroupId::SONG, "swingInterval",
     "the encoder wraps over 1..9 and never offers 0 (off), though the list is 0..9", &swing_interval_differs},
    {SettingGroupId::SONG, "reverb/compressor/volume",
     "menu -1..50 truncates (stored / 21474836); no mapping kind expresses it", &reverb_volume_differs},
};

/// A row whose menu shows where legacy's relevance says it does not, or the reverse, in one named state of its owner.
/// The applicability spec asserts the difference exactly: legacy's verdict is `legacy_shows` and the row's is the
/// opposite, so an exception whose row comes to agree fails, as one that was never reached does.
struct AppliesException {
	NamesOwner owner;         ///< the owner whose table holds the row
	std::string_view fixture; ///< the state, by its fixture's name
	std::string_view path;
	bool legacy_shows; ///< legacy's verdict; the descriptor's is the opposite
	std::string_view reason;
};

/// On the whole kit, legacy asks the SELECTED row what plays (`soundEditor.currentArpSettings` reads a gate drum's
/// kind), so a gate row selected hides the whole kit's step repeat and shows the host rows a non-sound arp has. The
/// descriptor's conditions read the kit, whichever row is selected.
inline constexpr std::string_view kWholeKitGateOff = "the whole kit with a gate row selected";
inline constexpr std::string_view kWholeKitGateOn = "the whole kit with a gate row selected and its arp on";
inline constexpr std::string_view kHidesStepRepeat =
    "legacy reads the selected gate row's kind, and a gate row's arp has no step repeat; the whole kit's has";
inline constexpr std::string_view kShowsNonSoundRow =
    "legacy reads the selected gate row's kind, which shows the host row a non-sound arp has; the whole kit's arp is a "
    "sound's, which does not";
/// Decision 4: with affect-entire on, a kit row's direction shortcut edits the kit clip's direction, so the kit clip
/// has the row; legacy's whole-kit menu lists no direction item.
inline constexpr std::string_view kKitClipDirection =
    "the kit clip has a direction, which the row shortcut edits with affect-entire on (decision 4); legacy's whole-kit "
    "menu lists no direction item";
inline const AppliesException kAppliesExceptions[] = {
    {SettingGroupId::CLIP, "the whole kit", "sequenceDirection", false, kKitClipDirection},
    {SettingGroupId::CLIP, "the whole kit with its arp on", "sequenceDirection", false, kKitClipDirection},
    {SettingGroupId::CLIP, "the whole kit with its own stutter", "sequenceDirection", false, kKitClipDirection},
    {SettingGroupId::CLIP, kWholeKitGateOff, "sequenceDirection", false, kKitClipDirection},
    {SettingGroupId::CLIP, kWholeKitGateOn, "sequenceDirection", false, kKitClipDirection},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOff, "arpeggiator/stepRepeat", false, kHidesStepRepeat},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOff, "arpeggiator/rhythm", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOff, "arpeggiator/sequenceLength", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOff, "arpeggiator/noteProbability", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/stepRepeat", false, kHidesStepRepeat},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/rhythm", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/sequenceLength", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/ratchetAmount", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/noteProbability", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/ratchetProbability", true, kShowsNonSoundRow},
    {SettingGroupId::CLIP_ARP, kWholeKitGateOn, "arpeggiator/spreadGate", true, kShowsNonSoundRow},
};

/// The exception for @p family's row @p path in @p fixture, or nullptr. A row of the arp's tables is keyed by the arp
/// host's owner.
inline const AppliesException* applies_exception(Family family, std::string_view fixture, std::string_view path) {
	const std::vector<NamesOwner> owners = owners_of(family);
	for (const AppliesException& exception : kAppliesExceptions) {
		if (exception.fixture == fixture && exception.path == path
		    && std::ranges::find(owners, exception.owner) != owners.end()) {
			return &exception;
		}
	}
	return nullptr;
}

/// Whether @p path is a named skip of @p owner's table; a skip ending in '/' names every path under it.
inline bool skipped(const NamesOwner& owner, std::string_view path) {
	for (const Skip& skip : kSkips) {
		const bool path_matches = skip.path == path || (skip.path.ends_with('/') && path.starts_with(skip.path));
		if (path_matches && std::ranges::find(skip.owners, owner) != skip.owners.end()) {
			return true;
		}
	}
	return false;
}

/// The rows of the groups the range, read and write checks do not drive, each with the legacy item that shows it. They
/// are named and compared by name only.
inline const std::vector<Case>& group_cases() {
	static const std::vector<Case> cases{
	    {.path = "sequenceDirection", .family = Family::CLIP},
	    {.path = "sequenceDirection", .family = Family::NOTE_ROW, .where = Where::KIT_LANE},
	    {.path = "bendRange", .family = Family::NOTE_ROW, .where = Where::KIT_LANE},
	    {.path = "outputMpeY", .family = Family::INSTRUMENT_DEVICE, .where = Where::MIDI_CLIP},
	    {.path = "collapseAftertouch", .family = Family::INSTRUMENT_DEVICE, .where = Where::MIDI_CLIP},
	    {.path = "collapseMpe", .family = Family::INSTRUMENT_DEVICE, .where = Where::MIDI_CLIP},
	    {.path = "hideUnlabeledCC", .family = Family::INSTRUMENT_DEVICE, .where = Where::MIDI_CLIP},
	    {.path = "deviceDefinitionFileName", .family = Family::INSTRUMENT_DEVICE, .where = Where::MIDI_CLIP},
	    {.path = "mode", .family = Family::AUDIO_INPUT, .where = Where::AUDIO_CLIP},
	    {.path = "inputChannel", .family = Family::AUDIO_INPUT, .where = Where::AUDIO_CLIP},
	    {.path = "outputRecordingFrom", .family = Family::AUDIO_INPUT, .where = Where::AUDIO_CLIP},
	};
	return cases;
}

/// Every case of every family the range, read and write checks drive.
inline std::vector<const Case*> all_cases() {
	std::vector<const Case*> out;
	for (const auto* family :
	     {&voice_cases(), &oscillator_cases(), &lfo_and_modulator_cases(), &unison_and_midi_cases(), &effect_cases(),
	      &arp_cases(), &arp_host_cases(), &model_cases()}) {
		for (const Case& c : *family) {
			out.push_back(&c);
		}
	}
	return out;
}

/// Every case, the named-only ones included.
inline std::vector<const Case*> all_name_cases() {
	std::vector<const Case*> out = all_cases();
	for (const Case& c : group_cases()) {
		out.push_back(&c);
	}
	return out;
}

} // namespace descriptor_cases

using namespace descriptor_cases;
