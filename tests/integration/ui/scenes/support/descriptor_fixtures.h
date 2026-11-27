#pragma once

/// @file
/// @brief The states of an owner the setting rows are checked in: each fixture's name, where it is, which families'
///        rows it holds and how it is set up on top of `enter`'s. The applicability example and the menu oracle
///        freezer share them, so the state a reference row was taken in is the state the port is asked in.

#include "support/descriptor_cases.h"

#include "memory/heaps.h"
#include "model/sample/sample.h"

#include <algorithm>
#include <new>
#include <string_view>
#include <vector>

namespace descriptor_cases {

/// Rows whose items are compared for where they show alone: a range or read no row holds whole.
inline const std::vector<Case>& applicability_cases() {
	static const std::vector<Case> cases{
	    // Its menu shows one value over this level and the engine's sync type, which no song row holds.
	    {.path = "reverb/compressor/syncLevel", .family = Family::SONG, .where = Where::SONG},
	};
	return cases;
}

/// Whether some case shows @p family's row @p path with an item of its own.
inline bool has_item(Family family, std::string_view path) {
	std::vector<const Case*> cases = all_name_cases();
	for (const Case& c : applicability_cases()) {
		cases.push_back(&c);
	}
	return std::ranges::any_of(cases, [&](const Case* c) { return c->family == family && c->path == path; });
}

/// One state of an owner, the families whose rows it holds, and how it is set up on top of `enter`'s.
struct Fixture {
	std::string_view name;
	Where where;
	std::vector<Family> families;
	void (*setup)(const MenuContext& ctx, Song& song) = nullptr;
};

inline void set_word(Sound& sound, std::string_view path, std::string_view word) {
	sound_setting(sound, path, word);
}
inline void set_arp(const MenuContext& ctx, Song& song, std::string_view mode) {
	deluge::edits::arpSettingFor(*spec::menu_engine(ctx, song).arp, "arpeggiator/arpMode",
	                             Value::makeScalar(std::string{mode}));
}
inline void set_song_stutter(const MenuContext& ctx, Song& song, bool use) {
	deluge::edits::effectSettingFor(deluge::ui::scenes::spec::menu_engine(ctx, song).mod, "stutter/useSongStutter",
	                                Value::makeNumber(use ? 1 : 0));
}
/// A sound as `enter` leaves it, poly, its envelopes ADSR, its arp off, using the song's stutter.
inline void plain_sound(const MenuContext& ctx, Song& song) {
	set_word(*spec::menu_engine(ctx, song).sound, "polyphonic", "poly");
	for (const std::string_view mode :
	     {"envelope1Settings/mode", "envelope2Settings/mode", "envelope3Settings/mode", "envelope4Settings/mode"}) {
		set_word(*spec::menu_engine(ctx, song).sound, mode, "adsr");
	}
	set_arp(ctx, song, "off");
	set_song_stutter(ctx, song, true);
}
/// The whole kit as `enter` leaves it, with its arp in @p arp_mode and using the song's stutter.
inline void plain_kit(const MenuContext& ctx, Song& song, std::string_view arp_mode) {
	set_arp(ctx, song, arp_mode);
	set_song_stutter(ctx, song, true);
}
inline void osc_types(const MenuContext& ctx, Song& song, OscType first, OscType second) {
	set_word(*spec::menu_engine(ctx, song).sound, "osc1/type", deluge::instruments::classic::to_string(first));
	set_word(*spec::menu_engine(ctx, song).sound, "osc2/type", deluge::instruments::classic::to_string(second));
}

inline const std::vector<Family> kSoundFamilies{Family::INSTRUMENT, Family::SOUND_ROUTING, Family::EFFECT, Family::ARP,
                                                Family::ARP_HOST,   Family::CLIP_VALUES,   Family::CLIP};
inline const std::vector<Family> kLaneFamilies{Family::INSTRUMENT, Family::SOUND_ROUTING, Family::EFFECT,
                                               Family::ARP,        Family::ARP_HOST,      Family::NOTE_ROW};
inline const std::vector<Family> kNonSoundLaneFamilies{Family::ARP, Family::ARP_HOST, Family::NOTE_ROW};
inline const std::vector<Family> kWholeKitFamilies{Family::EFFECT, Family::ARP, Family::ARP_HOST, Family::CLIP_VALUES,
                                                   Family::CLIP};
inline const std::vector<Family> kNonAudioTrackFamilies{Family::ARP, Family::ARP_HOST, Family::CLIP_VALUES,
                                                        Family::INSTRUMENT_DEVICE, Family::CLIP};
inline const std::vector<Family> kAudioFamilies{Family::AUDIO_CLIP, Family::EFFECT, Family::AUDIO_INPUT, Family::CLIP};
inline const std::vector<Family> kSongFamilies{Family::SONG, Family::EFFECT};

inline void set_cv_channel(const MenuContext& ctx, Song& song, int32_t channel) {
	deluge::edits::trackSettingFor(*spec::menu_engine(ctx, song).output, "channel", Value::makeNumber(channel));
}
inline void set_song(std::string_view path, int32_t value) {
	deluge::edits::songSettingFor(path, Value::makeNumber(value));
}

/// A sample standing for a loaded file: legacy asks only whether a source's range holds one.
inline AudioFile* fake_file() {
	static AudioFile* file = [] {
		// Placed in the deluge heap and never freed, as the song fixtures are.
		return static_cast<AudioFile*>(new (deluge::memory::alloc_fast(sizeof(Sample))) Sample());
	}();
	return file;
}
/// Give source @p source of the sound @p ctx edits a loaded file.
inline void load_file(const MenuContext& ctx, Song& song, int32_t source) {
	spec::menu_engine(ctx, song).sound->getOrCreateFirstRange(source)->getAudioFileHolder()->audioFile = fake_file();
}
/// Take every fake file off the sound @p ctx edits, before its oscillator types change again.
inline void unload_files(const MenuContext& ctx, Song& song) {
	if (spec::menu_engine(ctx, song).sound == nullptr) {
		return;
	}
	for (SourceResources& source : spec::menu_engine(ctx, song).sound->sourceResources) {
		for (int32_t e = 0; e < std::ssize(source.ranges); ++e) {
			source.ranges.getElement(e)->getAudioFileHolder()->audioFile = nullptr;
		}
	}
}
/// The descriptor context @p where stands for, with the sources that hold a file as the host reads them off the sound.
inline SettingContext descriptor_context(const Fixture& fixture, const MenuContext& ctx, Family family) {
	SettingContext shown = context_of(fixture.where, family);
	Song& song = song_of(fixture.where);
	if (spec::menu_engine(ctx, song).sound != nullptr) {
		for (int32_t i = 0; i < static_cast<int32_t>(deluge::plugin::kMaxSoundSources); ++i) {
			if (spec::menu_engine(ctx, song).sound->sourceResources[i].hasAtLeastOneAudioFileLoaded()) {
				shown.loaded_sources |= static_cast<uint8_t>(1 << i);
			}
		}
	}
	return shown;
}

inline const std::vector<Fixture>& fixtures() {
	static const std::vector<Fixture> all{
	    {"a subtractive synth", Where::SYNTH, kSoundFamilies, [](const MenuContext& c, Song& s) { plain_sound(c, s); }},
	    {"an FM synth", Where::SYNTH_FM, kSoundFamilies, [](const MenuContext& c, Song& s) { plain_sound(c, s); }},
	    {"a ring-mod synth", Where::SYNTH_RINGMOD, kSoundFamilies,
	     [](const MenuContext& c, Song& s) { plain_sound(c, s); }},
	    {"a mono synth", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_word(*spec::menu_engine(c, s).sound, "polyphonic", "mono");
	     }},
	    {"a legato synth", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_word(*spec::menu_engine(c, s).sound, "polyphonic", "legato");
	     }},
	    {"a synth playing samples with no file", Where::SYNTH_SAMPLE, kSoundFamilies,
	     [](const MenuContext& c, Song& s) { plain_sound(c, s); }},
	    {"a synth playing a sample and a wavetable", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SAMPLE, OscType::WAVETABLE);
	     }},
	    {"a synth playing wavetables", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::WAVETABLE, OscType::SINE);
	     }},
	    {"a synth playing the inputs", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::INPUT_L, OscType::INPUT_STEREO);
	     }},
	    {"a synth playing the right input", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::INPUT_R, OscType::SQUARE);
	     }},
	    {"a synth with its own stutter", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_song_stutter(c, s, false);
	     }},
	    // main's DX7 on oscillator 1 beside a saw; here the DX7 is the sound's engine, the saw parked on oscillator 2.
	    {"a synth with a DX7", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SQUARE, OscType::SAW);
		     set_word(*spec::menu_engine(c, s).sound, "mode", "dx7");
	     }},
	    {"a synth with its arp on", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_arp(c, s, "arp");
	     }},
	    {"a synth with other LFO waves", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_word(*spec::menu_engine(c, s).sound, "lfo1/type",
		              deluge::instruments::classic::to_string(LFOType::SINE));
		     set_word(*spec::menu_engine(c, s).sound, "lfo2/type",
		              deluge::instruments::classic::to_string(LFOType::RANDOM_WALK));
		     set_word(*spec::menu_engine(c, s).sound, "lfo3/type",
		              deluge::instruments::classic::to_string(LFOType::WARBLER));
	     }},
	    {"an FM synth whose first oscillator holds a sample", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SAMPLE, OscType::SQUARE);
		     set_word(*spec::menu_engine(c, s).sound, "mode", "fm");
	     }},
	    {"an FM synth whose oscillators hold wavetables", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::WAVETABLE, OscType::WAVETABLE);
		     set_word(*spec::menu_engine(c, s).sound, "mode", "fm");
	     }},
	    {"a synth playing loaded samples", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SAMPLE, OscType::SAMPLE);
		     load_file(c, s, 0);
		     load_file(c, s, 1);
	     }},
	    {"a synth playing a loaded wavetable and a sample with no file", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::WAVETABLE, OscType::SAMPLE);
		     load_file(c, s, 0);
	     }},
	    {"a synth with a loaded file on a square oscillator", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SAMPLE, OscType::WAVETABLE);
		     load_file(c, s, 0);
		     osc_types(c, s, OscType::SQUARE, OscType::WAVETABLE);
	     }},
	    {"an FM synth whose oscillators hold a loaded wavetable and an empty one", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::WAVETABLE, OscType::WAVETABLE);
		     load_file(c, s, 1);
		     set_word(*spec::menu_engine(c, s).sound, "mode", "fm");
	     }},
	    {"an FM synth whose first oscillator holds a loaded sample", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SAMPLE, OscType::SQUARE);
		     load_file(c, s, 0);
		     set_word(*spec::menu_engine(c, s).sound, "mode", "fm");
	     }},
	    {"a kit's sound row", Where::KIT_LANE, kLaneFamilies, [](const MenuContext& c, Song& s) { plain_sound(c, s); }},
	    {"a kit's sound row with its own stutter", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_song_stutter(c, s, false);
	     }},
	    {"a kit's sound row playing a loaded sample", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     osc_types(c, s, OscType::SAMPLE, OscType::SQUARE);
		     load_file(c, s, 0);
	     }},
	    {"a kit's sound row in FM", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_word(*spec::menu_engine(c, s).sound, "mode", "fm");
	     }},
	    {"a kit's sound row with its arp on", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_sound(c, s);
		     set_arp(c, s, "arp");
	     }},
	    {"a kit's MIDI row", Where::MIDI_DRUM, kNonSoundLaneFamilies,
	     [](const MenuContext& c, Song& s) { set_arp(c, s, "off"); }},
	    {"a kit's MIDI row with its arp on", Where::MIDI_DRUM, kNonSoundLaneFamilies,
	     [](const MenuContext& c, Song& s) { set_arp(c, s, "arp"); }},
	    {"a kit's gate row", Where::GATE_DRUM, kNonSoundLaneFamilies,
	     [](const MenuContext& c, Song& s) { set_arp(c, s, "off"); }},
	    {"a kit's gate row with its arp on", Where::GATE_DRUM, kNonSoundLaneFamilies,
	     [](const MenuContext& c, Song& s) { set_arp(c, s, "arp"); }},
	    {"the whole kit", Where::KIT, kWholeKitFamilies, [](const MenuContext& c, Song& s) { plain_kit(c, s, "off"); }},
	    {"the whole kit with its arp on", Where::KIT, kWholeKitFamilies,
	     [](const MenuContext& c, Song& s) { plain_kit(c, s, "arp"); }},
	    {"the whole kit with its own stutter", Where::KIT, kWholeKitFamilies,
	     [](const MenuContext& c, Song& s) {
		     plain_kit(c, s, "off");
		     set_song_stutter(c, s, false);
	     }},
	    // Legacy reads the selected row's kind on the whole kit; the descriptor reads the kit's.
	    {"the whole kit with a gate row selected", Where::KIT_GATE_ROW, kWholeKitFamilies,
	     [](const MenuContext& c, Song& s) { plain_kit(c, s, "off"); }},
	    {"the whole kit with a gate row selected and its arp on", Where::KIT_GATE_ROW, kWholeKitFamilies,
	     [](const MenuContext& c, Song& s) { plain_kit(c, s, "arp"); }},
	    {"a MIDI track", Where::MIDI_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c, Song& s) {
		     set_arp(c, s, "off");
		     set_cv_channel(c, s, 0);
	     }},
	    {"a MIDI track with its arp on", Where::MIDI_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c, Song& s) {
		     set_arp(c, s, "arp");
		     set_cv_channel(c, s, 0);
	     }},
	    // Channel 3 is stored as the word a CV track stores for both its channels: only a CV track offers a second
	    // channel's source.
	    {"a MIDI track on channel 3", Where::MIDI_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c, Song& s) {
		     set_arp(c, s, "off");
		     set_cv_channel(c, s, both);
	     }},
	    {"a CV track on both channels", Where::CV_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c, Song& s) {
		     set_arp(c, s, "off");
		     set_cv_channel(c, s, both);
	     }},
	    {"a CV track on one channel with its arp on", Where::CV_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c, Song& s) {
		     set_arp(c, s, "arp");
		     set_cv_channel(c, s, one);
	     }},
	    {"an audio clip", Where::AUDIO_CLIP, kAudioFamilies,
	     [](const MenuContext& c, Song& s) {
		     deluge::edits::audioInputSettingFor(static_cast<AudioOutput&>(*spec::menu_engine(c, s).output),
		                                         "inputChannel", Value::makeScalar("left"));
	     }},
	    {"an audio clip recording another track", Where::AUDIO_CLIP, kAudioFamilies,
	     [](const MenuContext& c, Song& s) {
		     deluge::edits::audioInputSettingFor(static_cast<AudioOutput&>(*spec::menu_engine(c, s).output),
		                                         "inputChannel", Value::makeScalar("specificTrack"));
	     }},
	    {"the song with the freeverb", Where::SONG, kSongFamilies,
	     [](const MenuContext&, Song&) {
		     set_song("reverb/model", 0);
		     set_song("reverb/compressor/volume", -1);
	     }},
	    {"the song with the mutable reverb, ducking at a set volume", Where::SONG, kSongFamilies,
	     [](const MenuContext&, Song&) {
		     set_song("reverb/model", 1);
		     set_song("reverb/compressor/volume", 0);
	     }},
	    {"the song with its reverb ducking automatic, as the menu writes it", Where::SONG, kSongFamilies,
	     [](const MenuContext&, Song&) {
		     set_song("reverb/model", 1);
		     set_song("reverb/compressor/volume", -21474836);
	     }},
	    {"the song with the digital reverb", Where::SONG, kSongFamilies,
	     [](const MenuContext&, Song&) {
		     set_song("reverb/model", 2);
		     set_song("reverb/compressor/volume", -1);
	     }},
	};
	return all;
}

/// Whether @p path is the partner of another row of @p family's table.
inline bool is_partner(Family family, std::string_view path) {
	return std::ranges::any_of(table_of(family), [&](const SettingInfo& row) {
		return deluge::plugin::mapping_of(row).partner_path() == path;
	});
}

} // namespace descriptor_cases
