/// @file
/// @brief Every setting row a menu shows, against the legacy menu item that shows it.
///
/// For each row with one legacy item, driven through `soundEditor.current*` as the sound editor drives it:
///   * the descriptor's menu range (`engine::menu_range`) is the item's `getMinValue`/`getMaxValue`, or its options'
///     count;
///   * `engine::to_menu` at the stored edges (and the values a family makes interesting) is the item's
///     `readCurrentValue` after the stored value is set through the row's Edit;
///   * `engine::from_menu` at the menu edges is what the item's `writeCurrentValue` stores.
///
/// A CHOICE row's menu lists the choices that apply in its context (`engine::setting_choice_applies`), in order, and
/// its menu value is a choice's position in that list. Each context-dependent list is driven in each of its contexts:
/// a track and a kit lane, ring mod, DX7's eligibility and the audio inputs.
///
/// A row with no single legacy item (a partner row, a row several items show together, a row only a gesture
/// changes) is a named skip, with its reason.
///
/// Whether a row's menu shows (`engine::setting_applies`) is legacy's: over a set of fixtures (each synth mode,
/// polyphony and oscillator type, a kit's sound, MIDI and gate rows and the whole kit, MIDI and CV tracks, an audio
/// clip and the song), a row applies exactly where one of its items is shown, an item being shown when it and every
/// submenu over it in the root `SoundEditor::setup` opens there is relevant (`isRelevant`).
///
/// @note The SPEC may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "support/descriptor_cases.h"

#include <algorithm>
#include <new>
#include <optional>

namespace {

/// Run @p cases, with the audio inputs unplugged.
void run(auto& self, const std::vector<Case>& cases) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : cases) {
		check(self, c);
	}
}

// ---------------------------------------------------------------------------------------------
// Where a row's menu shows.

/// `Submenu::items` is protected; named through a derived class, as `ValuePeek` names `writeCurrentValue`.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(const legacy::Submenu& menu) {
		return menu.*(&SubmenuPeek::items);
	}
};

/// Rows whose items are compared for where they show alone: a range or read no row holds whole.
const std::vector<Case>& applicability_cases() {
	static const std::vector<Case> cases{
	    // Its menu shows one value over this level and the engine's sync type, which no song row holds.
	    {.path = "reverb/compressor/syncLevel",
	     .family = Family::SONG,
	     .item = &reverbSidechainSyncMenu,
	     .where = Where::SONG},
	};
	return cases;
}

/// Whether @p item is a case's item or a submenu with one under it.
bool leads_to_a_case(const ::MenuItem* item) {
	static std::set<const ::MenuItem*> cases = [] {
		std::set<const ::MenuItem*> out;
		for (const Case* c : all_name_cases()) {
			out.insert(c->item);
		}
		for (const Case& c : applicability_cases()) {
			out.insert(c.item);
		}
		return out;
	}();
	if (cases.contains(item)) {
		return true;
	}
	const auto* submenu = dynamic_cast<const legacy::Submenu*>(item);
	return submenu != nullptr && std::ranges::any_of(SubmenuPeek::items_of(*submenu), leads_to_a_case);
}

/// Every case item under @p menu that legacy shows, and the submenus over them: relevant, under submenus that are all
/// relevant. A submenu at depth @p depth lists its items with `soundEditor.navigationDepth` at that depth, as the
/// sound editor does. Only the way to a case's item is asked: legacy asks an item when its submenu is open, and the
/// rest (a MIDI row's actions, say) is no row's.
void walk(::MenuItem* menu, uint8_t depth, std::set<const ::MenuItem*>& shown) {
	auto* submenu = dynamic_cast<legacy::Submenu*>(menu);
	if (submenu == nullptr) {
		return;
	}
	for (::MenuItem* item : SubmenuPeek::items_of(*submenu)) {
		soundEditor.navigationDepth = depth;
		if (item == nullptr || !leads_to_a_case(item)
		    || !item->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			continue;
		}
		shown.insert(item);
		walk(item, depth + 1, shown);
	}
}

/// The root `SoundEditor::setup` opens for @p ctx.
::MenuItem* legacy_root(const MenuContext& ctx) {
	using Root = MenuContext::Root;
	switch (ctx.root) {
	case Root::Sound:
		return &soundEditorRootMenu;
	case Root::Drum:
		return &soundEditorRootMenuDrum;
	case Root::MidiOrCv:
		return &soundEditorRootMenuMIDIOrCV;
	case Root::MidiDrum:
		return &soundEditorRootMenuMidiDrum;
	case Root::GateDrum:
		return &soundEditorRootMenuGateDrum;
	case Root::AudioClip:
		return &soundEditorRootMenuAudioClip;
	case Root::Song:
		return &soundEditorRootMenuSongView;
	case Root::KitGlobalFx:
		return &soundEditorRootMenuKitGlobalFX;
	default:
		return nullptr;
	}
}

/// One state of an owner, the families whose rows it holds, and how it is set up on top of `enter`'s.
struct Fixture {
	std::string_view name;
	Where where;
	std::vector<Family> families;
	void (*setup)(const MenuContext& ctx) = nullptr;
	/// The scope the host passes, where it is not the one `context_of` gives the rig.
	std::optional<SettingContext::Owner> owner{};
};

void set_word(Sound& sound, std::string_view path, std::string_view word) {
	sound_setting(sound, path, word);
}
void set_arp(const MenuContext& ctx, std::string_view mode) {
	deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/arpMode", Value::makeScalar(std::string{mode}));
}
void set_song_stutter(const MenuContext& ctx, bool use) {
	deluge::edits::effectSettingFor(*ctx.mod, "stutter/useSongStutter", Value::makeNumber(use ? 1 : 0));
}
/// A sound as `enter` leaves it, poly, its arp off, using the song's stutter.
void plain_sound(const MenuContext& ctx) {
	set_word(*ctx.sound, "polyphonic", "poly");
	set_arp(ctx, "off");
	set_song_stutter(ctx, true);
}
void osc_types(const MenuContext& ctx, OscType first, OscType second) {
	set_word(*ctx.sound, "osc1/type", deluge::instrument::to_string(first));
	set_word(*ctx.sound, "osc2/type", deluge::instrument::to_string(second));
}

const std::vector<Family> kSoundFamilies{Family::INSTRUMENT, Family::EFFECT,      Family::ARP,
                                         Family::ARP_HOST,   Family::CLIP_VALUES, Family::CLIP};
const std::vector<Family> kLaneFamilies{Family::INSTRUMENT, Family::EFFECT, Family::ARP, Family::ARP_HOST,
                                        Family::NOTE_ROW};
const std::vector<Family> kNonSoundLaneFamilies{Family::ARP, Family::ARP_HOST, Family::NOTE_ROW};
const std::vector<Family> kWholeKitFamilies{Family::EFFECT, Family::ARP, Family::ARP_HOST, Family::CLIP_VALUES,
                                            Family::CLIP};
const std::vector<Family> kNonAudioTrackFamilies{Family::ARP, Family::ARP_HOST, Family::CLIP_VALUES,
                                                 Family::INSTRUMENT_DEVICE, Family::CLIP};
const std::vector<Family> kAudioFamilies{Family::AUDIO_CLIP, Family::EFFECT, Family::AUDIO_INPUT, Family::CLIP};
const std::vector<Family> kSongFamilies{Family::SONG, Family::EFFECT};

void set_cv_channel(const MenuContext& ctx, int32_t channel) {
	deluge::edits::trackSettingFor(*ctx.output, "channel", Value::makeNumber(channel));
}
void set_song(std::string_view path, int32_t value) {
	deluge::edits::songSettingFor(path, Value::makeNumber(value));
}

/// A sample standing for a loaded file: legacy asks only whether a source's range holds one.
AudioFile* fake_file() {
	static AudioFile* file = [] {
		// Placed in the deluge heap and never freed, as the song fixtures are.
		return static_cast<AudioFile*>(new (deluge::memory::alloc_fast(sizeof(Sample))) Sample());
	}();
	return file;
}
/// Give source @p source of the sound @p ctx edits a loaded file.
void load_file(const MenuContext& ctx, int32_t source) {
	ctx.sound->sources[source].getOrCreateFirstRange()->getAudioFileHolder()->audioFile = fake_file();
}
/// Take every fake file off the sound @p ctx edits, before its oscillator types change again.
void unload_files(const MenuContext& ctx) {
	if (ctx.sound == nullptr) {
		return;
	}
	for (Source& source : ctx.sound->sources) {
		for (int32_t e = 0; e < std::ssize(source.ranges); ++e) {
			source.ranges.getElement(e)->getAudioFileHolder()->audioFile = nullptr;
		}
	}
}
/// The descriptor context @p where stands for, with the sources that hold a file as the host reads them off the sound.
SettingContext descriptor_context(const Fixture& fixture, const MenuContext& ctx) {
	SettingContext shown = context_of(fixture.where);
	if (fixture.owner) {
		shown.owner = *fixture.owner;
	}
	if (ctx.sound != nullptr) {
		for (int32_t i = 0; i < static_cast<int32_t>(deluge::engine::kMaxSoundSources); ++i) {
			if (ctx.sound->sources[i].hasAtLeastOneAudioFileLoaded()) {
				shown.loaded_sources |= static_cast<uint8_t>(1 << i);
			}
		}
	}
	return shown;
}

const std::vector<Fixture>& fixtures() {
	static const std::vector<Fixture> all{
	    {"a subtractive synth", Where::SYNTH, kSoundFamilies, [](const MenuContext& c) { plain_sound(c); }},
	    {"an FM synth", Where::SYNTH_FM, kSoundFamilies, [](const MenuContext& c) { plain_sound(c); }},
	    {"a ring-mod synth", Where::SYNTH_RINGMOD, kSoundFamilies, [](const MenuContext& c) { plain_sound(c); }},
	    {"a mono synth", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_word(*c.sound, "polyphonic", "mono");
	     }},
	    {"a legato synth", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_word(*c.sound, "polyphonic", "legato");
	     }},
	    {"a synth playing samples with no file", Where::SYNTH_SAMPLE, kSoundFamilies,
	     [](const MenuContext& c) { plain_sound(c); }},
	    {"a synth playing a sample and a wavetable", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::SAMPLE, OscType::WAVETABLE);
	     }},
	    {"a synth playing wavetables", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::WAVETABLE, OscType::SINE);
	     }},
	    {"a synth playing the inputs", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::INPUT_L, OscType::INPUT_STEREO);
	     }},
	    {"a synth playing the right input", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::INPUT_R, OscType::SQUARE);
	     }},
	    {"a synth with its own stutter", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_song_stutter(c, false);
	     }},
	    {"a synth with a DX7", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::DX7, OscType::SAW);
	     }},
	    {"a synth with its arp on", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_arp(c, "arp");
	     }},
	    {"a synth with other LFO waves", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_word(*c.sound, "lfo1/type", deluge::instrument::to_string(LFOType::SINE));
		     set_word(*c.sound, "lfo2/type", deluge::instrument::to_string(LFOType::RANDOM_WALK));
		     set_word(*c.sound, "lfo3/type", deluge::instrument::to_string(LFOType::WARBLER));
	     }},
	    {"an FM synth whose first oscillator holds a sample", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::SAMPLE, OscType::SQUARE);
		     set_word(*c.sound, "mode", "fm");
	     }},
	    {"an FM synth whose oscillators hold wavetables", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::WAVETABLE, OscType::WAVETABLE);
		     set_word(*c.sound, "mode", "fm");
	     }},
	    {"a synth playing loaded samples", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::SAMPLE, OscType::SAMPLE);
		     load_file(c, 0);
		     load_file(c, 1);
	     }},
	    {"a synth playing a loaded wavetable and a sample with no file", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::WAVETABLE, OscType::SAMPLE);
		     load_file(c, 0);
	     }},
	    {"a synth with a loaded file on a square oscillator", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::SAMPLE, OscType::WAVETABLE);
		     load_file(c, 0);
		     osc_types(c, OscType::SQUARE, OscType::WAVETABLE);
	     }},
	    {"an FM synth whose oscillators hold a loaded wavetable and an empty one", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::WAVETABLE, OscType::WAVETABLE);
		     load_file(c, 1);
		     set_word(*c.sound, "mode", "fm");
	     }},
	    {"an FM synth whose first oscillator holds a loaded sample", Where::SYNTH, kSoundFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::SAMPLE, OscType::SQUARE);
		     load_file(c, 0);
		     set_word(*c.sound, "mode", "fm");
	     }},
	    {"a kit's sound row", Where::KIT_LANE, kLaneFamilies, [](const MenuContext& c) { plain_sound(c); }},
	    {"a kit's sound row with its own stutter", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_song_stutter(c, false);
	     }},
	    {"a kit's sound row playing a loaded sample", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     osc_types(c, OscType::SAMPLE, OscType::SQUARE);
		     load_file(c, 0);
	     }},
	    {"a kit's sound row in FM", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_word(*c.sound, "mode", "fm");
	     }},
	    {"a kit's sound row with its arp on", Where::KIT_LANE, kLaneFamilies,
	     [](const MenuContext& c) {
		     plain_sound(c);
		     set_arp(c, "arp");
	     }},
	    {"a kit's MIDI row", Where::MIDI_DRUM, kNonSoundLaneFamilies, [](const MenuContext& c) { set_arp(c, "off"); }},
	    {"a kit's MIDI row with its arp on", Where::MIDI_DRUM, kNonSoundLaneFamilies,
	     [](const MenuContext& c) { set_arp(c, "arp"); }},
	    {"a kit's gate row", Where::GATE_DRUM, kNonSoundLaneFamilies, [](const MenuContext& c) { set_arp(c, "off"); }},
	    {"a kit's gate row with its arp on", Where::GATE_DRUM, kNonSoundLaneFamilies,
	     [](const MenuContext& c) { set_arp(c, "arp"); }},
	    {"the whole kit", Where::KIT, kWholeKitFamilies, [](const MenuContext& c) { set_arp(c, "off"); }},
	    {"the whole kit with its arp on", Where::KIT, kWholeKitFamilies,
	     [](const MenuContext& c) { set_arp(c, "arp"); }},
	    // The whole kit's arp is its clip's (CLIP_ARP): a host may pass the clip as its scope.
	    {"the whole kit's arp, as its clip's",
	     Where::KIT,
	     {Family::ARP, Family::ARP_HOST},
	     [](const MenuContext& c) { set_arp(c, "arp"); },
	     Owner::CLIP},
	    {"a MIDI track", Where::MIDI_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c) {
		     set_arp(c, "off");
		     set_cv_channel(c, 0);
	     }},
	    {"a MIDI track with its arp on", Where::MIDI_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c) {
		     set_arp(c, "arp");
		     set_cv_channel(c, 0);
	     }},
	    // Channel 3 is stored as the word a CV track stores for both its channels: only a CV track offers a second
	    // channel's source.
	    {"a MIDI track on channel 3", Where::MIDI_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c) {
		     set_arp(c, "off");
		     set_cv_channel(c, both);
	     }},
	    {"a CV track on both channels", Where::CV_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c) {
		     set_arp(c, "off");
		     set_cv_channel(c, both);
	     }},
	    {"a CV track on one channel with its arp on", Where::CV_CLIP, kNonAudioTrackFamilies,
	     [](const MenuContext& c) {
		     set_arp(c, "arp");
		     set_cv_channel(c, one);
	     }},
	    {"an audio clip", Where::AUDIO_CLIP, kAudioFamilies,
	     [](const MenuContext& c) {
		     deluge::edits::audioInputSettingFor(static_cast<AudioOutput&>(*c.output), "inputChannel",
		                                         Value::makeScalar("left"));
	     }},
	    {"an audio clip recording another track", Where::AUDIO_CLIP, kAudioFamilies,
	     [](const MenuContext& c) {
		     deluge::edits::audioInputSettingFor(static_cast<AudioOutput&>(*c.output), "inputChannel",
		                                         Value::makeScalar("specificTrack"));
	     }},
	    {"the song with the freeverb", Where::SONG, kSongFamilies,
	     [](const MenuContext&) {
		     set_song("reverb/model", 0);
		     set_song("reverb/compressor/volume", -1);
	     }},
	    {"the song with the mutable reverb, ducking at a set volume", Where::SONG, kSongFamilies,
	     [](const MenuContext&) {
		     set_song("reverb/model", 1);
		     set_song("reverb/compressor/volume", 0);
	     }},
	    {"the song with its reverb ducking automatic, as the menu writes it", Where::SONG, kSongFamilies,
	     [](const MenuContext&) {
		     set_song("reverb/model", 1);
		     set_song("reverb/compressor/volume", -21474836);
	     }},
	    {"the song with the digital reverb", Where::SONG, kSongFamilies,
	     [](const MenuContext&) {
		     set_song("reverb/model", 2);
		     set_song("reverb/compressor/volume", -1);
	     }},
	};
	return all;
}

/// The legacy items that show @p family's row @p path, from the cases.
std::vector<const ::MenuItem*> items_for(Family family, std::string_view path) {
	std::vector<const ::MenuItem*> out;
	std::vector<const Case*> cases = all_name_cases();
	for (const Case& c : applicability_cases()) {
		cases.push_back(&c);
	}
	for (const Case* c : cases) {
		if (c->family == family && c->path == path) {
			out.push_back(c->item);
		}
	}
	return out;
}

/// Whether @p path is the partner of another row of @p family's table.
bool is_partner(Family family, std::string_view path) {
	return std::ranges::any_of(table_of(family),
	                           [&](const SettingInfo& row) { return deluge::engine::mapping_of(row).partner == path; });
}

std::string verdict(std::string_view fixture, std::string_view path, bool applies) {
	return std::string{fixture} + ": " + std::string{path} + (applies ? " shows" : " does not show");
}

} // namespace

// clang-format off
describe descriptor_legacy_voice("The voice rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as polyphony, voice priority, voice count, sidechain send, synth mode and master transpose do", _ {
		run(self, voice_cases());
	});
	it("offers choke on a kit lane alone", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::instrument::kSettings, "polyphonic");
		expect(applicable(row, Where::SYNTH).size()).to_equal(4u);
		expect(applicable(row, Where::KIT_LANE).size()).to_equal(5u);
	});
});

describe descriptor_legacy_oscillators("The oscillator rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the oscillator and sample items do", _ {
		run(self, oscillator_cases());
	});
	it("lists the oscillator types as legacy does with the inputs plugged in, on a track and a kit lane", _ {
		AudioEngine::micPluggedIn = true;
		check(self, oscillator_cases()[0]);
		check(self, oscillator_cases()[1]);
		check(self, oscillator_cases()[3]);
		AudioEngine::micPluggedIn = false;
	});
	it("drops the sample and inputs under ring mod, and DX7 off a track's first oscillator", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::instrument::kSettings, "osc1/type");
		expect(list_of(applicable(row, Where::SYNTH_RINGMOD))).to_equal(std::string{"0,1,2,3,4,5,6,"});
		expect(list_of(applicable(row, Where::SYNTH))).to_equal(std::string{"0,1,2,3,4,5,6,7,8,9,"});
		const SettingInfo& second = *deluge::engine::find_setting(deluge::instrument::kSettings, "osc2/type");
		expect(list_of(applicable(second, Where::SYNTH))).to_equal(std::string{"0,1,2,3,4,5,6,7,9,"});
		expect(list_of(applicable(row, Where::KIT_LANE))).to_equal(std::string{"0,1,2,3,4,5,6,7,9,"});
		const MenuContext& ctx = enter(Where::SYNTH);
		expect(list_of(applicable(row, SettingContext{.audio_inputs_present = true}, ctx, Where::SYNTH)))
		    .to_equal(std::string{"0,1,2,3,4,5,6,7,8,9,10,11,"});
	});
});

describe descriptor_legacy_lfos_and_modulators("The LFO and FM modulator rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the LFO and modulator items do", _ {
		run(self, lfo_and_modulator_cases());
	});
});

describe descriptor_legacy_unison_and_midi("The unison and MIDI output rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the unison and MIDI output items do", _ {
		run(self, unison_and_midi_cases());
	});
});

describe descriptor_legacy_effects("The effect rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the filter, delay, sidechain, compressor and stutter items do", _ {
		run(self, effect_cases());
	});
});

describe descriptor_legacy_arp("The arp rows' descriptors against legacy's items", $ {
	it("ranges, reads and writes as the arp and randomizer items do", _ {
		run(self, arp_cases());
	});
	it("ranges, reads and writes the host rows as a MIDI clip's arp and randomizer items do", _ {
		run(self, arp_host_cases());
	});
	it("offers the as-played and pattern note modes off a kit lane alone", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::engine::kArpSettingRows, "arpeggiator/noteMode");
		expect(applicable(row, Where::SYNTH).size()).to_equal(9u);
		expect(applicable(row, Where::KIT_LANE).size()).to_equal(7u);
	});
});

describe descriptor_legacy_model("The model groups' rows against legacy's items", $ {
	it("ranges, reads and writes as the reverb, swing, audio clip and clip value items do", _ {
		run(self, model_cases());
	});
});

describe descriptor_legacy_applicability("Where each row's menu shows, against legacy's relevance", $ {
	it("shows each row exactly where legacy shows one of its items, in every fixture", _ {
		AudioEngine::micPluggedIn = false;
		AudioEngine::lineInPluggedIn = false;
		for (const Fixture& fixture : fixtures()) {
			enter(fixture.where);
			fixture.setup(settle(fixture.where));
			const MenuContext& ctx = settle(fixture.where);
			std::set<const ::MenuItem*> shown;
			walk(legacy_root(ctx), 0, shown);
			if (std::ranges::find(fixture.families, Family::NOTE_ROW) != fixture.families.end()) {
				walk(&noteRowEditorRootMenu, 0, shown);
			}
			const SettingContext descriptor_ctx = descriptor_context(fixture, ctx);
			for (Family family : fixture.families) {
				const SpecReader owner{ctx, fixture.where, family};
				for (const SettingInfo& row : table_of(family)) {
					const std::vector<const ::MenuItem*> items = items_for(family, row.path);
					if (items.empty()) {
						// A row no item shows says nothing of where it shows, but for a partner, which follows its row.
						// A sound's effect rows are compared as the EFFECT family's.
						const bool effect = family == Family::INSTRUMENT
						                    && deluge::engine::find_setting(deluge::effects::kEffectSettings, row.path) != nullptr;
						const bool silent = effect || row.extras == nullptr
						                    || (row.extras->applies_when.empty() && row.extras->applies_any.empty()
						                        && row.extras->choice_when.empty());
						expect(std::string{row.path} + (silent || is_partner(family, row.path) ? " silent" : " has conditions"))
						    .to_equal(std::string{row.path} + " silent");
						continue;
					}
					const bool legacy = std::ranges::any_of(items, [&](const ::MenuItem* item) { return shown.contains(item); });
					const bool applies = deluge::engine::setting_applies(row, descriptor_ctx, owner.reader());
					expect(verdict(fixture.name, row.path, applies)).to_equal(verdict(fixture.name, row.path, legacy));
				}
				expect(std::string{fixture.name} + " reads " + (owner.unknown.empty() ? "" : owner.unknown.front()))
				    .to_equal(std::string{fixture.name} + " reads ");
			}
			unload_files(ctx);
		}
	});
	it("shows a partner row exactly where its row shows, in every fixture", _ {
		for (const Fixture& fixture : fixtures()) {
			enter(fixture.where);
			fixture.setup(settle(fixture.where));
			const MenuContext& ctx = settle(fixture.where);
			const SettingContext descriptor_ctx = descriptor_context(fixture, ctx);
			for (Family family : fixture.families) {
				const SpecReader owner{ctx, fixture.where, family};
				for (const SettingInfo& row : table_of(family)) {
					const std::string_view partner_path = deluge::engine::mapping_of(row).partner;
					const SettingInfo* partner = partner_path.empty() ? nullptr : deluge::engine::find_setting(table_of(family), partner_path);
					if (partner == nullptr) {
						continue;
					}
					const bool applies = deluge::engine::setting_applies(row, descriptor_ctx, owner.reader());
					expect(verdict(fixture.name, partner->path, deluge::engine::setting_applies(*partner, descriptor_ctx, owner.reader())))
					    .to_equal(verdict(fixture.name, partner->path, applies));
				}
			}
			unload_files(ctx);
		}
	});
	it("offers no DX7 to the whole kit's sounds", _ {
		const SettingInfo& row = *deluge::engine::find_setting(deluge::instrument::kSettings, "osc1/type");
		const MenuContext& ctx = enter(Where::SYNTH);
		const SettingContext whole_kit{.owner = Owner::KIT, .instrument = SettingContext::Instrument::KIT};
		expect(list_of(applicable(row, whole_kit, ctx, Where::SYNTH))).to_equal(std::string{"0,1,2,3,4,5,6,7,9,"});
	});
});

describe descriptor_legacy_validity("Every table's conditions", $ {
	it("name rows of their own table, with values those rows store, in every setting group and the arp's", _ {
		// Every id a SettingGroupId can hold: a group added later is walked with the rest.
		using deluge::project::SettingGroupId;
		for (int32_t id = 0; id <= std::numeric_limits<uint8_t>::max(); ++id) {
			const std::span<const SettingInfo> table = deluge::project::table_of(static_cast<SettingGroupId>(id));
			if (table.empty()) {
				continue;
			}
			expect("group " + std::to_string(id) + (deluge::engine::conditions_are_valid(table) ? " valid" : " invalid"))
			    .to_equal("group " + std::to_string(id) + " valid");
		}
		expect(deluge::engine::conditions_are_valid(deluge::engine::kArpRows)).to_be_true();
		expect(deluge::engine::conditions_are_valid(deluge::instrument::kSettings)).to_be_true();
		expect(deluge::engine::conditions_are_valid(deluge::effects::kEffectSettings)).to_be_true();
	});
});

describe descriptor_legacy_coverage("Every engine and model row with a menu", $ {
	it("is compared with its legacy item, or named as a skip with its reason", _ {
		std::set<std::string_view> covered;
		for (const Case* c : all_cases()) {
			covered.insert(c->path);
		}
		using deluge::project::SettingGroupId;
		const std::span<const SettingInfo> tables[] = {
		    deluge::instrument::kSettings,
		    deluge::effects::kEffectSettings,
		    deluge::engine::kArpSettingRows,
		    deluge::engine::kArpHostRows,
		    deluge::project::table_of(SettingGroupId::SONG),
		    deluge::project::table_of(SettingGroupId::AUDIO_CLIP),
		    deluge::project::table_of(SettingGroupId::CLIP_VALUES),
		};
		for (auto table : tables) {
			for (const SettingInfo& row : table) {
				const bool named = covered.contains(row.path) || skipped(row.path);
				expect(std::string{row.path} + (named ? " named" : " unnamed")).to_equal(std::string{row.path} + " named");
			}
		}
	});
	it("names each exception a coherence check must carry", _ {
		for (const Skip& exception : kExceptions) {
			expect(exception.reason.empty()).to_be_false();
		}
	});
});
// clang-format on

DELUGE_SPEC(descriptor_legacy_spec, descriptor_legacy_voice, descriptor_legacy_oscillators,
            descriptor_legacy_lfos_and_modulators, descriptor_legacy_unison_and_midi, descriptor_legacy_effects,
            descriptor_legacy_arp, descriptor_legacy_model, descriptor_legacy_applicability, descriptor_legacy_validity,
            descriptor_legacy_coverage)
