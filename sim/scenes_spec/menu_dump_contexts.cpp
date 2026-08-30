/// @file
/// @brief `deluge::scenes::menu_dump::build_contexts()` -- outside `src/scenes/` on purpose (see
///        `scenes/menu/menu_dump_contexts.h`'s note): this is the one place `deluge_menu_dump`
///        reaches for `sim/scenes_spec/support/song_fixture.h`, mirroring
///        `sim/scenes_spec/tables_3_spec.cpp`'s `roots()` fixture list (the ten contexts Task 8
///        built) minus the legacy `::MenuItem*` cross-check that spec needs and this dump does
///        not.
///
/// Physically in `sim/scenes_spec/` (not `src/scenes/menu/`, where `menu_dump_main.cpp` lives) so
/// its own `#include "support/song_fixture.h"` resolves the same way every `*_spec.cpp` here
/// does -- relative to this file's own directory -- with no extra include-path plumbing.

#include "scenes/menu/menu_dump_contexts.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/settings/runtime_feature_settings.h"

#include "support/song_fixture.h"

namespace deluge::scenes::menu_dump {

namespace spec = deluge::scenes::spec;
using deluge::scenes::menu::MenuContext;

std::vector<NamedContext> build_contexts() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	// The settings root's community-features page indexes an un-`init`ed `RuntimeFeatureSetting`'s
	// empty `options` vector otherwise (`tables_3_spec.cpp`'s own comment on this same call).
	runtimeFeatureSettings.init();

	std::vector<NamedContext> out;

	spec::SongFixture synth = spec::make_synth_song();
	out.push_back({"Sound", menu::context_for(*synth.song, synth.clip, 0, false, false, false)});

	spec::SongFixture kit = spec::make_kit_song();
	SoundDrum* drum = spec::add_drum<SoundDrum>(kit);
	static_cast<Kit*>(kit.instrument)->selectedDrum = drum;
	out.push_back({"Drum", menu::context_for(*kit.song, kit.clip, 0, false, false, false)});

	spec::SongFixture midi = spec::make_midi_song();
	out.push_back({"MidiOrCv", menu::context_for(*midi.song, midi.clip, 0, false, false, false)});

	spec::SongFixture midi_kit = spec::make_kit_song();
	MIDIDrum* midi_drum = spec::add_drum<MIDIDrum>(midi_kit);
	static_cast<Kit*>(midi_kit.instrument)->selectedDrum = midi_drum;
	out.push_back({"MidiDrum", menu::context_for(*midi_kit.song, midi_kit.clip, 0, false, false, false)});

	spec::SongFixture gate_kit = spec::make_kit_song();
	GateDrum* gate_drum = spec::add_drum<GateDrum>(gate_kit);
	static_cast<Kit*>(gate_kit.instrument)->selectedDrum = gate_drum;
	out.push_back({"GateDrum", menu::context_for(*gate_kit.song, gate_kit.clip, 0, false, false, false)});

	spec::AudioSongFixture audio = spec::make_audio_clip_song();
	out.push_back({"AudioClip", menu::context_for(*audio.song, audio.clip, 0, false, false, false)});

	spec::SongFixture song_view = spec::make_synth_song();
	out.push_back({"Performance", menu::context_for(*song_view.song, nullptr, 0, /*performance_view=*/true,
	                                                /*session_or_arranger_root=*/true, false)});
	out.push_back({"Song", menu::context_for(*song_view.song, nullptr, 0, false,
	                                         /*session_or_arranger_root=*/true, false)});

	spec::SongFixture kit_fx = spec::make_kit_song();
	(void)spec::add_drum<SoundDrum>(kit_fx);
	kit_fx.clip->affectEntire = true;
	out.push_back(
	    {"KitGlobalFx", menu::context_for(*kit_fx.song, kit_fx.clip, 0, false, false, /*kit_global_fx=*/true)});

	spec::SongFixture settings = spec::make_synth_song();
	out.push_back({"Settings", menu::context_for(*settings.song, nullptr, 0, false, false, false)});

	return out;
}

} // namespace deluge::scenes::menu_dump
