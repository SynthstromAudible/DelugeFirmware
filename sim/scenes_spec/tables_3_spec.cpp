/// @file
/// @brief The batch-3 menu tables -- the settings root and the nine other roots -- cross-checked
///        against the legacy menu items they replace.
///
/// Task 6 declared six subtrees and task 7 the sound-editor root; task 8 declares everything else
/// `SoundEditor::setup` can open: the kit-drum root, the MIDI/CV root (both of its titles), the
/// MIDI- and gate-drum roots, the audio-clip root, the performance-view root, the song root, the
/// kit global-FX root, and the settings root with its whole subtree.
///
/// The checks are `tables_2_spec`'s, generalised over **ten** roots rather than one -- each with
/// its own fixture and its own `MenuContext`, because a MIDI clip, an audio clip and the settings
/// root resolve to genuinely different contexts:
///
///   * every tree walks in lockstep with its legacy tree (same children, same order, same count);
///   * each node's name and title are the legacy item's own `getName()` / `getTitle()`;
///   * each node's `ParamRef` is the legacy item's own `getParamKind()` / `getParamIndex()`;
///   * `bindings::read` equals the legacy `readCurrentValue()` + `getValue()` on the same fixture;
///   * each `Kind::Enum`'s names are the legacy `Selection::getOptions(FULL)` strings;
///   * each node's `relevant` predicate agrees with the legacy `isRelevant()` override;
///   * `bindings::write` leaves the model where the legacy `writeCurrentValue()` leaves it, for
///     **every** node with a `set` hook (task-7 ruling);
///
/// plus what only a complete tree can be asked: `MenuScene::roots()` resolves all ten,
/// `validate_tree` over every root under every context, the spec E §4 `Kind`-count rule, and the
/// new `Flag::ReverbUpdateWithHook` audit.
///
/// @note The SPEC may include `gui/menu_item/` and `gui/ui/menus.h`; the library may not
///       (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule
///       `tables_1_spec.cpp` and `tables_2_spec.cpp` run under.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

// The chaining group's legacy headers render through an unqualified `OLED::main`, which resolves
// inside `gui/ui/menus.cpp` only because that file has a file-scope
// `using namespace deluge::hid::display`. This spec has no such directive, so name the type in the
// namespace those headers are compiled in -- the workaround `tables_1_spec.cpp` established for
// `gui/menu_item/filter_route.h`.
namespace deluge::gui::menu_item {
using deluge::hid::display::OLED;
} // namespace deluge::gui::menu_item
#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"

#include "processing/engines/cv_engine.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace legacy = deluge::gui::menu_item;

/// @name The chaining group's legacy globals.
///
/// `cvSubmenu`, `gateModeMenu` and `gateOffTimeMenu` are children of nothing (`menus.cpp:1223`,
/// `:1224`, `:1232`) -- legacy reaches all three only through a `selectButtonPress()` override, so
/// unlike everything else in this spec they cannot be found by walking `settingsRootMenu`.
/// `cvSubmenu` is `extern` in `cv/selection.h` already; the rest are named here.
/// @{
extern legacy::cv::Selection cvSelectionMenu;
extern legacy::gate::Selection gateSelectionMenu;
extern legacy::cv::Volts cvVoltsMenu;
extern legacy::cv::Transpose cvTransposeMenu;
/// @}

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;

/// @brief Read a `Submenu`'s protected child list -- `tables_2_spec`'s `SubmenuPeek`, verbatim and
///        for the same [class.protected] reason.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(legacy::Submenu& menu) { return menu.*(&SubmenuPeek::items); }
};

/// @brief Reach `Output::activeClip`, which is `protected` (`model/output.h:235`) and whose only
///        public setter, `Output::setActiveClip`, also re-sends MIDI program changes and wants a
///        timeline-counter model stack. Same [class.protected] route `SubmenuPeek` uses: the
///        pointer-to-member is formed inside a class derived from `Output`, and its type is still
///        `Clip* Output::*`. Never instantiated -- `Output` is abstract and has no default
///        constructor, which is exactly why this is only ever used to name the member.
struct OutputPeek : ::Output {
	static Clip*& active_clip_of(::Output& out) { return out.*(&OutputPeek::activeClip); }
};

/// @brief Call a legacy item's protected `writeCurrentValue()` -- `tables_2_spec`'s `ValuePeek`.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief Point the `soundEditor` globals at whatever @p ctx is editing -- `tables_2_spec`'s
///        `install`, plus `currentMIDICable` (which batch 3's MIDI-device items would read, and
///        which `context_for` never sets).
void install(const MenuContext& ctx, Song* song) {
	currentSong = song;
	song->setCurrentClip(ctx.clip);
	soundEditor.currentSound = ctx.sound;
	soundEditor.currentModControllable = ctx.mod;
	soundEditor.currentParamManager = ctx.params;
	soundEditor.currentSourceIndex = ctx.source_index;
	soundEditor.currentSource = ctx.source;
	soundEditor.currentMultiRange = ctx.range;
	soundEditor.currentMultiRangeIndex = ctx.range_index;
	soundEditor.currentArpSettings = ctx.arp;
	soundEditor.currentPriority = ctx.priority;
	soundEditor.currentSidechain = ctx.sidechain;
	soundEditor.currentSampleControls = ctx.sample_controls;
	soundEditor.allowsNoteTails = true;
	if (ctx.sound != nullptr) {
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);
		soundEditor.allowsNoteTails = ctx.sound->allowNoteTails(stack->addSoundFlags(), true);
	}
	// `SoundEditor::editingKitAffectEntire()` (`sound_editor.cpp:165-167`) reads this member, and the
	// whole kit-global-FX arpeggiator page's relevance turns on it. `context_for` takes the same fact
	// as its `kit_global_fx` argument and reports it back as `is_kit_global_fx`.
	soundEditor.setupKitGlobalFXMenu = ctx.is_kit_global_fx;
	currentUIMode = ctx.affect_entire_held ? UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR : 0;
	soundEditor.navigationDepth = 0;
}

/// @brief One root: its legacy object, its node, and the fixture a `MenuContext` was resolved from.
struct Root {
	const char* name;
	::MenuItem* legacy;
	const Node* node;
	MenuContext ctx;
	Song* song;
};

/// @brief Every root batch 3 declares, plus the sound root batch 2 did (so the `Kind` count and the
///        whole-tree validation see the complete tree).
///
/// The fixtures are built once and never torn down (`song_fixture.h`'s ownership note). Each is the
/// clip shape `SoundEditor::setup` would have been opened over, so `context_for` picks the arm the
/// root belongs to -- which the `resolves each root from its own fixture's context` example asserts
/// directly rather than assuming.
std::vector<Root>& roots() {
	static std::vector<Root> list = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		// `deluge_boot()` calls this (`deluge.cpp`); nothing else in this spec's world does, and the
		// community-features page is exactly the part of the settings root that reads it -- an
		// un-`init`ed `RuntimeFeatureSetting` has an empty `options` vector, which legacy's own
		// `Setting::readCurrentValue` indexes unconditionally (`runtime_feature/setting.cpp:23-28`).
		runtimeFeatureSettings.init();
		std::vector<Root> r;

		{ // sound (batch 2's root, re-checked here as part of the whole tree)
			static spec::SongFixture synth = spec::make_synth_song();
			r.push_back({"sound", &soundEditorRootMenu, &nodes::sound_root,
			             menu::context_for(*synth.song, synth.clip, 0, false, false, false), synth.song});
		}
		{ // kit row with a SoundDrum
			static spec::SongFixture kit = spec::make_kit_song();
			static SoundDrum* drum = spec::add_drum<SoundDrum>(kit);
			static_cast<Kit*>(kit.instrument)->selectedDrum = drum;
			r.push_back({"drum", &soundEditorRootMenuDrum, &nodes::root_drum,
			             menu::context_for(*kit.song, kit.clip, 0, false, false, false), kit.song});
		}
		{ // MIDI clip
			static spec::SongFixture midi = spec::make_midi_song();
			r.push_back({"midi/cv", &soundEditorRootMenuMIDIOrCV, &nodes::root_midi_or_cv,
			             menu::context_for(*midi.song, midi.clip, 0, false, false, false), midi.song});
		}
		{ // kit row with a MIDIDrum, and one with a GateDrum
			static spec::SongFixture midi_kit = spec::make_kit_song();
			static MIDIDrum* midi_drum = spec::add_drum<MIDIDrum>(midi_kit);
			static_cast<Kit*>(midi_kit.instrument)->selectedDrum = midi_drum;
			r.push_back({"midi drum", &soundEditorRootMenuMidiDrum, &nodes::root_midi_drum,
			             menu::context_for(*midi_kit.song, midi_kit.clip, 0, false, false, false), midi_kit.song});

			static spec::SongFixture gate_kit = spec::make_kit_song();
			static GateDrum* gate_drum = spec::add_drum<GateDrum>(gate_kit);
			static_cast<Kit*>(gate_kit.instrument)->selectedDrum = gate_drum;
			r.push_back({"gate drum", &soundEditorRootMenuGateDrum, &nodes::root_gate_drum,
			             menu::context_for(*gate_kit.song, gate_kit.clip, 0, false, false, false), gate_kit.song});
		}
		{ // audio clip
			static spec::AudioSongFixture audio = spec::make_audio_clip_song();
			r.push_back({"audio clip", &soundEditorRootMenuAudioClip, &nodes::root_audio_clip,
			             menu::context_for(*audio.song, audio.clip, 0, false, false, false), audio.song});
		}
		{ // performance view and the session/arranger root: no clip
			static spec::SongFixture song_view = spec::make_synth_song();
			r.push_back({"performance", &soundEditorRootMenuPerformanceView, &nodes::root_performance,
			             menu::context_for(*song_view.song, nullptr, 0, /*performance_view=*/true,
			                               /*session_or_arranger_root=*/true, false),
			             song_view.song});
			r.push_back(
			    {"song", &soundEditorRootMenuSongView, &nodes::root_song,
			     menu::context_for(*song_view.song, nullptr, 0, false, /*session_or_arranger_root=*/true, false),
			     song_view.song});
		}
		{ // a kit on affect-entire
			static spec::SongFixture kit_fx = spec::make_kit_song();
			(void)spec::add_drum<SoundDrum>(kit_fx);
			kit_fx.clip->affectEntire = true;
			r.push_back({"kit global fx", &soundEditorRootMenuKitGlobalFX, &nodes::root_kit_global_fx,
			             menu::context_for(*kit_fx.song, kit_fx.clip, 0, false, false, /*kit_global_fx=*/true),
			             kit_fx.song});
		}
		{ // settings: no clip, not a session root
			static spec::SongFixture settings = spec::make_synth_song();
			r.push_back({"settings", &settingsRootMenu, &nodes::root_settings,
			             menu::context_for(*settings.song, nullptr, 0, false, false, false), settings.song});
		}
		return r;
	}();
	return list;
}

/// @brief The CV fixture, used only by the two examples about the MIDI/CV root's second title and
///        the CV2 source item -- it is not a root of its own (`Root::MidiOrCv` covers both).
struct CvRig {
	spec::SongFixture song;
	MenuContext ctx{};
};
CvRig& cv_fixture() {
	static CvRig c = [] {
		CvRig k;
		k.song = spec::make_cv_song();
		k.ctx = menu::context_for(*k.song.song, k.song.clip, 0, false, false, false);
		return k;
	}();
	return c;
}

std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief One `(node, legacy item)` pair, and which root it was reached from.
struct Pair {
	const Node* node;
	::MenuItem* item;
	const Root* root;
	int32_t depth;
	int32_t navigation_depth() const { return depth > 0 ? depth - 1 : 0; }
};

bool walk(const Node& node, ::MenuItem& item, const Root& root, std::vector<Pair>& out, std::string& where,
          int32_t depth = 0) {
	out.push_back(Pair{&node, &item, &root, depth});
	if (node.kind != menu::Kind::Submenu) {
		// Fix round 1, minor 4: returning here without looking at the legacy side would let a
		// `Kind::Custom` placeholder stand in for a legacy `Submenu` and silently drop that whole
		// legacy subtree from every comparison below. No node in these tables does that, and this is
		// what keeps it that way.
		if (dynamic_cast<legacy::Submenu*>(&item) != nullptr) {
			where = std::string{root.name} + ": non-submenu node stands in for legacy submenu "
			        + std::string{item.getName()};
			return false;
		}
		return true;
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(&item);
	if (submenu == nullptr) {
		where = std::string{root.name} + ": node is a submenu but legacy item is not: " + std::string{item.getName()};
		return false;
	}
	const auto& kids = SubmenuPeek::items_of(*submenu);
	if (kids.size() != node.children.size()) {
		where = std::string{root.name} + ": child count differs under " + std::string{item.getName()} + " ("
		        + std::to_string(kids.size()) + " legacy vs " + std::to_string(node.children.size()) + " port)";
		return false;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		if (node.children[i] == nullptr || kids[i] == nullptr) {
			where = std::string{root.name} + ": null child under " + std::string{item.getName()};
			return false;
		}
		if (!walk(*node.children[i], *kids[i], root, out, where, depth + 1)) {
			return false;
		}
	}
	return true;
}

struct Walk {
	std::vector<Pair> pairs;
	std::string problem;
};

/// @brief Every `(node, legacy item)` pair across every root, or an empty list plus a reason.
const Walk& tree() {
	static Walk w = [] {
		Walk result;
		for (const Root& root : roots()) {
			install(root.ctx, root.song);
			if (!walk(*root.node, *root.legacy, root, result.pairs, result.problem)) {
				result.pairs.clear();
				break;
			}
		}
		return result;
	}();
	return w;
}

std::optional<int32_t> legacy_value(::MenuItem* item) {
	if (auto* v = dynamic_cast<legacy::Value<int32_t>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue();
	}
	if (auto* v = dynamic_cast<legacy::Value<bool>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue() ? 1 : 0;
	}
	return std::nullopt;
}

bool legacy_write(::MenuItem* item, int32_t v) {
	if (auto* n = dynamic_cast<legacy::Value<int32_t>*>(item); n != nullptr) {
		n->setValue(v);
		ValuePeek<int32_t>::write(*n);
		return true;
	}
	if (auto* b = dynamic_cast<legacy::Value<bool>*>(item); b != nullptr) {
		b->setValue(v != 0);
		ValuePeek<bool>::write(*b);
		return true;
	}
	return false;
}

/// @brief Legacy items whose `getName()`/`getTitle()` is built at render time rather than being the
///        l10n id the constructor was given -- so one static `Node::name` cannot match it.
///
/// Keyed on the node (review round 1, M7's rule), and every entry names its mechanism:
///
///   * the sixteen `midiFollowChannelTrack*Menu` are `FormattedTitle`s with the track index
///     substituted (`midi/follow/follow_channel.h:142-149`);
///   * the community-feature items take their name from the runtime settings table
///     (`runtime_feature/setting.cpp:40-46`), which is a `l10n::String` -- so those DO match and are
///     not listed; only the ones legacy formats are.
bool has_dynamic_name(const Node& node) {
	static const std::set<const Node*> kDynamic = {
	    &nodes::midi_follow_track1,
	    &nodes::midi_follow_track2,
	    &nodes::midi_follow_track3,
	    &nodes::midi_follow_track4,
	    &nodes::midi_follow_track5,
	    &nodes::midi_follow_track6,
	    &nodes::midi_follow_track7,
	    &nodes::midi_follow_track8,
	    &nodes::midi_follow_track9,
	    &nodes::midi_follow_track10,
	    &nodes::midi_follow_track11,
	    &nodes::midi_follow_track12,
	    &nodes::midi_follow_track13,
	    &nodes::midi_follow_track14,
	    &nodes::midi_follow_track15,
	    &nodes::midi_follow_track16,
	    // batch 1's and batch 2's, reachable again from these roots
	    &nodes::lpf_morph,
	    &nodes::hpf_morph,
	    &nodes::source0,
	    &nodes::source1,
	    &nodes::env1,
	    &nodes::env2,
	    &nodes::env3,
	    &nodes::env4,
	    &nodes::source0_volume,
	    &nodes::source1_volume,
	    &nodes::modulator0_volume,
	    &nodes::modulator1_volume,
	    &nodes::sample0_recorder,
	    &nodes::sample1_recorder,
	    &nodes::mod_fx_depth,
	    &nodes::mod_fx_feedback,
	    &nodes::mod_fx_offset,
	    &nodes::reverb_room_size,
	    &nodes::reverb_width,
	    // the unpatched twins of the three mod-FX items and the two morph params
	    &nodes::global_mod_fx_depth,
	    &nodes::global_lpf_morph,
	    &nodes::global_hpf_morph,
	};
	return kDynamic.contains(&node);
}

/// @brief Legacy `isRelevant` overrides that read UI globals this spec does not stand up, so the
///        port deliberately answers differently.
///
///   * `song::ConfigureMacros` and `song::MidiLearn` are `getRootUI() == &sessionView`
///     (`song/configure_macros.h:24-26`, `song/midi_learn.h:24-26`) -- a root-UI read, which task-3
///     ruling 2 keeps out of `src/scenes` entirely. Both nodes are E2 placeholders anyway.
bool relevance_needs_ui(const Node& node) {
	return &node == &nodes::configure_song_macros || &node == &nodes::song_midi_learn;
}

/// @brief Legacy `isRelevant` overrides that **crash** when the context has no `Sound`.
///
/// `osc::AudioRecorder::isRelevant` is `static_cast<Sound*>(modControllable)->getSynthMode()`
/// (`osc/audio_recorder.h:60-63`) with no null test -- and `soundEditorRootDrumActionsMenu`
/// (`menus.cpp:1767`), which holds both sample recorders, is a child of **`soundEditorRootMenuMidiDrum`
/// and `soundEditorRootMenuGateDrum`** (`:1967`, `:1975`), whose `setup()` arm leaves
/// `currentModControllable` null (`sound_editor.cpp:1672-1674`). So opening ACTIONS on a MIDI or
/// gate drum row dereferences a null `Sound` in the shipping firmware: a legacy bug this port
/// found, recorded here rather than reproduced. The port's own answer is `relevance::is_subtractive`,
/// which returns false for a null sound, and the example below asserts exactly that instead of
/// calling the legacy override.
bool legacy_relevance_needs_sound(const Pair& p) {
	return p.root->ctx.sound == nullptr && (p.node == &nodes::sample0_recorder || p.node == &nodes::sample1_recorder);
}

/// @brief Nodes whose **legacy** `writeCurrentValue()` this spec cannot drive, and why. Every one
///        reaches a `gui/` or `hid/` global that only a booted app has.
///
/// **This list suppresses the legacy call only** (fix round 2, I1/I3). It used to skip the whole
/// `(node, item)` pair, which took the *port's* hook out of the oracle along with legacy's -- and
/// the read comparison does not make up the difference: `bindings::read(arp_mode)` observes only
/// `arp->mode`, which `set_arp_mode` assigns trivially, so `updatePresetFromCurrentSettings()`,
/// `updateSettingsFromCurrentPreset()`, `flagForceArpRestart` and the whole
/// `silence_for_arp_toggle` block (`nodes/sound_arp.cpp:80-149`) were asserted by nothing at all.
/// Deleting any of them left the suite green. So the `drives the port hook of every node the legacy
/// write comparison excludes` example below drives **every one of these fourteen port hooks on its
/// own** and asserts what each actually persists, and the loop over this list is what makes adding a
/// fifteenth exclusion without covering it a failure rather than a quiet hole.
///
/// Fix round 1, I3: `default_velocity` and `trigger_in_ppqn` were on this list and should not have
/// been. `defaults::Velocity::writeCurrentValue` (`defaults/velocity.h:13-16`) is `FlashStorage`
/// plus `currentSong->setDefaultVelocityForAllInstruments` -- pure model, over the very song
/// `install()` points `currentSong` at, and the one place the port's `ctx.song` and legacy's
/// `currentSong` can be *seen* to agree. `trigger::in::PPQN`'s resync is guarded by
/// `isExternalClockActive() && usingAnalogClockInput` (`trigger/in/ppqn.h:24-26`), both false in
/// this fixture. Both are driven now.
constexpr const Node* kLegacyWriteNeedsUi[] = {
    &nodes::synth_mode,                // `view.setKnobIndicatorLevels()` (`synth_mode.h:57`)
    &nodes::arp_mode,                  // `allNotesOff` over `soundEditor.getCurrentModelStack`
    &nodes::arp_preset,                // the same block (`preset_mode.h:50-70`)
    &nodes::mod_fx_type,               // `display->displayError` (`mod_fx/type.h:42,52`)
    &nodes::audio_clip_reverse,        // `uiNeedsRendering(&audioClipView)` (`reverse.h:52`)
    &nodes::midi_clock_out,            // `playbackHandler.setMidiOutClockMode` sends real MIDI
    &nodes::midi_clock_in,             // ... and `setMidiInClockEnabled` re-syncs playback
    &nodes::trigger_out_ppqn,          // `resyncAnalogOutTicksToInternalTicks` (`out/ppqn.h:24`)
    &nodes::default_pad_brightness,    // `PadLEDs::setBrightnessLevel` (`pad_brightness.h:26`)
    &nodes::default_menu_highlighting, // `Canvas::menuHighlightingInverts`
    &nodes::screensaver_mode,          // `Screensaver::settingsChanged()`
    &nodes::screensaver_timeout,       // ... and again
    &nodes::feature_sticky_shift,      // `Buttons::clearShiftSticky()`
    &nodes::feature_rounded_corners,   // `Canvas::roundedCornersEnabled`
};

bool legacy_write_needs_ui(const Node& node) {
	for (const Node* n : kLegacyWriteNeedsUi) {
		if (n == &node) {
			return true;
		}
	}
	return false;
}

/// @brief The root fixture named @p name (`roots()`'s own `Root::name`), installed and ready.
///
/// Aborts the example loudly rather than returning null: every caller below names a root this file
/// declares, so a miss is a typo, not a case to handle.
const Root& installed_root(std::string_view name) {
	for (const Root& r : roots()) {
		if (name == r.name) {
			install(r.ctx, r.song);
			return r;
		}
	}
	static const Root kNone{"<none>", nullptr, nullptr, {}, nullptr};
	return kNone;
}

/// @brief Is @p p's node on screen in its root's own context?
///
/// The guard every value comparison below needs, and the one legacy applies for free: `Submenu`
/// only ever calls `readCurrentValue()` on an item its `isRelevant` admitted
/// (`gui/menu_item/submenu.cpp:63`). Reading a hidden one is not merely pointless, it is unsafe --
/// `UnpatchedParam::readCurrentValue` casts `summaries[0].paramCollection` to an `UnpatchedParamSet`
/// (`unpatched_param.cpp:37`), and on a MIDI clip that summary holds a `MIDIParamCollection`
/// (`param_manager.cpp:66-69`). The randomizer's and arpeggiator's unpatched params are declared
/// under the MIDI/CV root precisely so their `midi_cv` twins can sit beside them, and every one of
/// them is `isRelevant() == false` there.
bool relevant_here(const Pair& p) {
	return p.node->relevant == nullptr || p.node->relevant(p.root->ctx);
}

/// @brief A sentinel value inside @p r that is not @p v, or nothing when the range is a point.
///
/// Fix round 1, I2: the write comparison needs a starting state the value under test is *not*
/// already at, or a port hook that stores nothing looks identical to one that stores correctly.
std::optional<int32_t> sentinel_for(const menu::IntRange& r, int32_t v) {
	if (r.min == r.max) {
		return std::nullopt;
	}
	return v == r.min ? r.max : r.min;
}

/// @brief What the two write paths leave behind, each driven from the same starting state.
struct WriteProbe {
	bool ran = false;
	int32_t got_port = 0;
	int32_t want_port = 0;
	std::optional<int32_t> got_legacy{};
	std::optional<int32_t> want_legacy{};
};

/// @brief Drive @p node's `set` hook and @p item's `writeCurrentValue()` **independently**, each
///        from @p sentinel, and report what each left behind.
///
/// Fix round 1, I2. The original shape ran `legacy_write(v)` first and `bindings::write(v)` second,
/// then asserted the second changed nothing -- which a port hook that stores *nothing at all* also
/// satisfies, because legacy had already moved the model to `v`. Here each path starts from a state
/// that is not `v`, so a no-op port hook leaves `got_*` at the sentinel while `want_*` is at `v`.
///
/// @return `ran == false` for an item that holds no value at all (a submenu, or a bespoke one).
WriteProbe probe_write(const Node& node, ::MenuItem* item, const MenuContext& ctx, int32_t v, int32_t sentinel) {
	WriteProbe out;
	if (!legacy_write(item, sentinel)) {
		return out;
	}
	menu::bindings::write(node, ctx, v);
	out.got_port = menu::bindings::read(node, ctx);
	out.got_legacy = legacy_value(item);

	if (!legacy_write(item, sentinel) || !legacy_write(item, v)) {
		return out;
	}
	out.want_port = menu::bindings::read(node, ctx);
	out.want_legacy = legacy_value(item);
	out.ran = true;
	return out;
}

std::array<int32_t, 3> probe_values(const menu::IntRange& r) {
	return {r.min, static_cast<int32_t>((static_cast<int64_t>(r.min) + r.max) / 2), r.max};
}

/// @brief A `Shell` over `FakeSurfaceSet` with the application's scene registry, for the goldens.
struct Rig {
	const deluge::ui::shell::BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	deluge::ui::shell::Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	             const deluge::ui::shell::BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
	}
};

constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const deluge::ui::shell::BoardProfile& profile_64() {
	static const deluge::ui::shell::BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		deluge::ui::shell::BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief Is @p target named by some node's static `children` span, anywhere in `kAllRoots`?
///
/// `tables_2_spec.cpp` has the same helper for the arp half; the point of a chain-only page is that
/// the answer here is *no*, so both halves need to be able to ask.
bool reachable_by_children(const Node* target) {
	std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
	std::set<const Node*> seen;
	while (!stack.empty()) {
		const Node* n = stack.back();
		stack.pop_back();
		if (n == nullptr || !seen.insert(n).second) {
			continue;
		}
		for (const Node* c : n->children) {
			if (c == target) {
				return true;
			}
			stack.push_back(c);
		}
		for (const Node* t : n->chain_targets) {
			stack.push_back(t); // traversed, but never counted as a `children` hit
		}
	}
	return false;
}

} // namespace

// clang-format off
describe tables_3_shape("scenes::menu batch-3 tables: shape", $ {
	it("walks every root in lockstep with its legacy tree", _ {
		const Walk& w = tree();
		expect(w.problem).to_equal(std::string{});
		expect(w.pairs.empty()).to_be_false();
	});

	it("declares the settings root with the legacy child count", _ {
		// `menus.cpp`'s `settingsRootMenu` literal (`:2150-2166`) -- thirteen children, asserted
		// against the live object rather than a second copy of the list.
		const auto& kids = SubmenuPeek::items_of(settingsRootMenu);
		expect(kids.size()).to_equal(size_t(13));
		expect(nodes::root_settings.children.size()).to_equal(size_t(13));
	});

	it("resolves each root from its own fixture's context", _ {
		// `context_for`'s part 2 is what chooses a root; this is the check that each fixture really
		// is the clip shape that root belongs to, so the walks above are not comparing a tree
		// against a context it would never be opened in.
		const menu::MenuContext::Root want[] = {
		    menu::MenuContext::Root::Sound,      menu::MenuContext::Root::Drum,
		    menu::MenuContext::Root::MidiOrCv,   menu::MenuContext::Root::MidiDrum,
		    menu::MenuContext::Root::GateDrum,   menu::MenuContext::Root::AudioClip,
		    menu::MenuContext::Root::Performance, menu::MenuContext::Root::Song,
		    menu::MenuContext::Root::KitGlobalFx, menu::MenuContext::Root::Settings,
		};
		expect(roots().size()).to_equal(std::size(want));
		for (size_t i = 0; i < roots().size(); i++) {
			expect(roots()[i].ctx.root == want[i]).to_be_true();
			expect(roots()[i].ctx.resolved).to_be_true();
		}
	});

	it("fills MenuScene::roots() with one node per MenuContext::Root", _ {
		const std::span<const Node* const> r = menu::MenuScene::roots();
		expect(r.size()).to_equal(size_t(menu::MenuContext::Root::Count));
		for (const Root& root : roots()) {
			expect(r[size_t(root.ctx.root)] == root.node).to_be_true();
		}
	});

	it("carries both of the MIDI/CV root's titles, chosen by output type", _ {
		// `sound_editor.cpp:1726-1731` rewrites one object's title; the port has two nodes over one
		// child list, and `midi_or_cv_root` is the branch.
		expect(text_of(nodes::root_midi_or_cv.title))
		    .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_MIDI_INST_MENU_TITLE)});
		expect(text_of(nodes::root_cv.title))
		    .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_CV_INSTRUMENT)});
		expect(nodes::root_cv.children.data() == nodes::root_midi_or_cv.children.data()).to_be_true();

		const Root* midi = nullptr;
		for (const Root& root : roots()) {
			if (root.ctx.root == menu::MenuContext::Root::MidiOrCv) {
				midi = &root;
			}
		}
		expect(midi != nullptr).to_be_true();
		if (midi != nullptr) {
			expect(&nodes::midi_or_cv_root(midi->ctx) == &nodes::root_midi_or_cv).to_be_true();
		}
		expect(&nodes::midi_or_cv_root(cv_fixture().ctx) == &nodes::root_cv).to_be_true();
	});

	it("gives every node the legacy item's own name", _ {
		for (const Pair& p : tree().pairs) {
			if (has_dynamic_name(*p.node)) {
				continue;
			}
			install(p.root->ctx, p.root->song);
			expect(text_of(p.node->name) == std::string{p.item->getName()}).to_be_true();
		}
	});

	it("gives every node the legacy item's own title", _ {
		size_t compared = 0;
		for (const Pair& p : tree().pairs) {
			if (has_dynamic_name(*p.node)) {
				continue;
			}
			install(p.root->ctx, p.root->song);
			const std::string title = text_of(p.node->title);
			// A `*` in the resolved string is a `FormattedTitle` placeholder legacy substitutes an
			// index into at render time (`formatted_title.h`) -- batch 1's gap, recorded there.
			if (title.find('*') != std::string::npos) {
				continue;
			}
			expect(title == std::string{p.item->getTitle()}).to_be_true();
			compared++;
		}
		expect(compared > size_t(800)).to_be_true(); // 934 today, across the ten roots
	});

	it("gives every node the legacy item's own ParamRef, or none where legacy has none", _ {
		size_t compared = 0;
		for (const Pair& p : tree().pairs) {
			// A `Submenu` reports its first param child's kind, not one of its own; a
			// `Kind::Custom` node is an E2 placeholder with no binding at all.
			if (p.node->kind == menu::Kind::Submenu || p.node->kind == menu::Kind::Custom) {
				continue;
			}
			if (!p.node->bind.param.valid() || !relevant_here(p)) {
				continue;
			}
			install(p.root->ctx, p.root->song);
			// `audio_clip::Transpose` carries a **learn-only** `ParamRef` (`with_learn_param`): it is a
			// `Decimal`, so legacy's `getParamIndex()` is the base's invalid one and only
			// `getLearningThing()` (`audio_clip/transpose.h:41-45`) names the param. Compared there
			// instead, exactly as `tables_2_spec` compares the four `menu_item::Transpose` nodes.
			if (p.node->bind.get != nullptr) {
				auto* learner = dynamic_cast<::MenuItemWithCCLearning*>(p.item);
				expect(learner != nullptr).to_be_true();
				if (learner != nullptr) {
					expect(menu::bindings::learn_target(*p.node, p.root->ctx) == learner->getLearningThing())
					    .to_be_true();
				}
				continue;
			}
			expect(uint32_t(p.node->bind.param.index) == p.item->getParamIndex()).to_be_true();
			compared++;

			// `UnpatchedParam::getParamKind` (`unpatched_param.cpp:96-101`) answers by building
			// `SoundEditor::getCurrentModelStack`, which dereferences `getCurrentInstrumentClip()`
			// (`sound_editor.cpp:1983`) -- so it can only be asked where the fixture HAS a clip. The
			// song, performance and settings roots have none by construction (`context_for`'s
			// `clip == nullptr` arm is what selects them), so the *kind* half of this comparison is
			// restricted to the seven clip-backed roots; the *index* half above covers all ten.
			if (p.root->ctx.clip == nullptr) {
				continue;
			}
			const auto kind = p.item->getParamKind();
			if (kind == deluge::modulation::params::Kind::NONE) {
				continue;
			}
			const bool unpatched = kind == deluge::modulation::params::Kind::UNPATCHED_SOUND
			                       || kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
			if (unpatched) {
				expect(p.node->bind.param.kind == deluge::modulation::params::Kind::UNPATCHED_SOUND
				       || p.node->bind.param.kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL)
				    .to_be_true();
			}
			else {
				expect(p.node->bind.param.kind == kind).to_be_true();
			}
		}
		expect(compared > size_t(90)).to_be_true();
	});
});

describe tables_3_values("scenes::menu batch-3 tables: values", $ {
	it("reads what the legacy item reads, for every node that holds a value", _ {
		size_t compared = 0;
		for (const Pair& p : tree().pairs) {
			if (p.node->kind == menu::Kind::Custom || !relevant_here(p)) {
				continue; // an E2 placeholder, or an item this context does not show (see `relevant_here`)
			}
			install(p.root->ctx, p.root->song);
			const std::optional<int32_t> want = legacy_value(p.item);
			if (!want.has_value()) {
				continue; // a submenu, or a bespoke item with no value
			}
			expect(menu::bindings::read(*p.node, p.root->ctx) == *want).to_be_true();
			compared++;
		}
		// 581 today. A drop would mean the loop stopped reaching the leaves.
		expect(compared > size_t(500)).to_be_true();
	});

	it("names every enum option exactly as the legacy Selection does", _ {
		size_t compared = 0;
		for (const Pair& p : tree().pairs) {
			if (p.node->kind != menu::Kind::Enum || !relevant_here(p)) {
				continue;
			}
			// `defaults::Magnitude` is an `Enumeration`, not a `Selection`: it has no option *names*
			// at all, it draws `96 << value` (`defaults/magnitude.h:22-32`). Nothing to compare.
			auto* selection = dynamic_cast<legacy::Selection*>(p.item);
			if (selection == nullptr) {
				expect(p.node == &nodes::default_magnitude).to_be_true();
				continue;
			}
			install(p.root->ctx, p.root->song);
			const auto want = selection->getOptions(legacy::Selection::OptType::FULL);

			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t n = menu::bindings::names(*p.node, p.root->ctx, got, scratch);
			expect(n == want.size()).to_be_true();
			for (size_t i = 0; i < n && i < want.size(); i++) {
				expect(std::string{got[i]} == std::string{want[i]}).to_be_true();
			}
			compared++;
		}
		expect(compared > size_t(100)).to_be_true(); // 129 today
	});

	it("agrees with the legacy isRelevant in every root's own context", _ {
		size_t ever_true = 0;
		for (const Pair& p : tree().pairs) {
			if (relevance_needs_ui(*p.node)) {
				continue;
			}
			if (legacy_relevance_needs_sound(p)) {
				// See `legacy_relevance_needs_sound`: legacy would crash here, the port answers false.
				expect(p.node->relevant != nullptr && !p.node->relevant(p.root->ctx)).to_be_true();
				continue;
			}
			install(p.root->ctx, p.root->song);
			soundEditor.navigationDepth = p.navigation_depth();
			const bool want = p.item->isRelevant(p.root->ctx.mod, p.root->ctx.source_index);
			const bool got = p.node->relevant == nullptr || p.node->relevant(p.root->ctx);
			expect(got == want).to_be_true();
			if (got) {
				ever_true++;
			}
		}
		soundEditor.navigationDepth = 0;
		expect(ever_true > size_t(700)).to_be_true(); // 843 today -- the loop cannot go vacuous
	});

	it("refuses an unpatched param on a MIDI clip rather than reinterpreting its collection", _ {
		// Pre-review fix round. `ParamManager::setupMIDI` and `setupUnpatched`
		// (`modulation/params/param_manager.cpp:60-82`) both leave `expressionParamSetOffset == 1`
		// and `summaries[0].paramCollection` non-null, and `getUnpatchedParamSet()` is a bare cast of
		// that pointer -- so on a MIDI or CV clip an unpatched `ParamRef` read would reinterpret a
		// `MIDIParamCollection`. Batch 3 made that reachable: the MIDI/CV root carries the
		// randomizer's and the arpeggiator's unpatched params so their `midi_cv` twins can be
		// siblings. `bindings::holds_unpatched_params` is what refuses it.
		//
		// `read()`'s "cannot serve" answer is a literal 0, which no *served* param can produce: a
		// stored 0 scales to `kMidMenuValue` (25) through `computeCurrentValueForStandardMenuItem`,
		// and the pan scaling's own zero point is 0 stored -> 0 shown but those params are patched or
		// global, neither of which is reachable here. So 0 is the signature of the refusal, and
		// reverting the hardening turns these reads into whatever lands at the reinterpreted
		// `params[]` offset.
		const Root* midi = nullptr;
		const Root* sound = nullptr;
		for (const Root& root : roots()) {
			if (root.ctx.root == menu::MenuContext::Root::MidiOrCv) {
				midi = &root;
			}
			if (root.ctx.root == menu::MenuContext::Root::Sound) {
				sound = &root;
			}
		}
		expect(midi != nullptr && sound != nullptr).to_be_true();
		if (midi == nullptr || sound == nullptr) {
			return;
		}
		// The fixture really is a MIDI param manager, and it really does look servable structurally.
		expect(midi->ctx.params != nullptr).to_be_true();
		expect(midi->ctx.params->containsAnyParamCollectionsIncludingExpression()).to_be_true();

		// Fix round 1, I1. `summaries[1]` being non-null is NOT proof of a `PatchedParamSet`:
		// `setupMIDI` leaves `expressionParamSetOffset == 1` (`param_manager.cpp:66-69`), so
		// `ensureExpressionParamSetExists` puts an `ExpressionParamSet` there (`:289-298`) -- and
		// `bend_range::Main::writeCurrentValue` calls `getOrCreateExpressionParamSet()`
		// (`gui/menu_item/bend_range/main.h:30`) from `bendMenu`, which is a child of this very root.
		// So the scenario is reproduced here rather than assumed away: create the expression set the
		// way a user editing bend range would, then assert a PATCHED `ParamRef` is *still* refused.
		expect(midi->ctx.params->getOrCreateExpressionParamSet() != nullptr).to_be_true();
		expect(midi->ctx.params->summaries[1].paramCollection != nullptr).to_be_true();

		install(midi->ctx, midi->song);
		static constexpr Node kPatchedProbe =
		    menu::param(1, {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ});
		expect(menu::bindings::read(kPatchedProbe, midi->ctx)).to_equal(0);
		menu::bindings::write(kPatchedProbe, midi->ctx, 42);
		expect(menu::bindings::read(kPatchedProbe, midi->ctx)).to_equal(0);

		std::set<const Node*> refused;
		for (const Pair& p : tree().pairs) {
			if (p.root != midi || !p.node->bind.param.valid() || p.node->bind.set != nullptr) {
				continue;
			}
			install(midi->ctx, midi->song);
			expect(menu::bindings::read(*p.node, midi->ctx)).to_equal(0);
			menu::bindings::write(*p.node, midi->ctx, 42);
			expect(menu::bindings::read(*p.node, midi->ctx)).to_equal(0);
			refused.insert(p.node);
		}
		// Every unpatched param node the MIDI/CV root reaches -- fourteen randomizer and arpeggiator
		// leaves. A drop to zero would make the example vacuous.
		expect(refused.size() > size_t(10)).to_be_true(); // 14 today

		// The positive control: the very same nodes DO read and write on a context whose
		// `summaries[0]` really is an `UnpatchedParamSet`, so the refusal above is about the MIDI
		// manager and not about the nodes.
		size_t served = 0;
		install(sound->ctx, sound->song);
		for (const Pair& p : tree().pairs) {
			if (p.root != sound || !refused.contains(p.node)) {
				continue;
			}
			menu::bindings::write(*p.node, sound->ctx, 42);
			expect(menu::bindings::read(*p.node, sound->ctx)).to_equal(42);
			served++;
		}
		expect(served > size_t(10)).to_be_true(); // the same 14
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const Pair& p : tree().pairs) {
			if (p.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(p.node->relevant) != std::string_view{"?"}).to_be_true();
		}
	});
});

describe tables_3_validation("scenes::menu batch-3 tables: validation", $ {
	it("validates every root but settings clean apart from the nine legacy name collisions", _ {
		std::vector<MenuContext> contexts;
		std::vector<const Node*> without_settings;
		for (const Root& root : roots()) {
			contexts.push_back(root.ctx);
			if (root.node != &nodes::root_settings) {
				without_settings.push_back(root.node);
			}
		}
		without_settings.push_back(&nodes::root_cv);
		// Fix round 1, minor 5: `roots()` has no CV entry (`Root::MidiOrCv` covers both spellings),
		// so without this the CV context never reached the `EmptySubmenu` and `DuplicateName`
		// quantifiers -- and `root_cv`'s children are exactly the ones whose relevance differs
		// between a MIDI and a CV output (`cv2_source`, `is_midi_out`).
		contexts.push_back(cv_fixture().ctx);
		install(roots().front().ctx, roots().front().song);
		const auto issues = menu::validate_tree(
		    without_settings, contexts, nodes::kRangeDependentNodes, nodes::kHalfPrecisionNodes,
		    menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		size_t duplicates = 0;
		size_t others = 0;
		for (const menu::Issue& i : issues) {
			(i.kind == menu::Issue::Kind::DuplicateName ? duplicates : others)++;
		}
		// Sixteen: `tables_2_spec`'s nine, plus seven the kit-drum root repeats. The rule is per
		// *parent*, and `soundEditorRootMenuDrum` (`menus.cpp:1805`) is a second parent over the same
		// children -- so `env1Menu`..`env4Menu` collide pairwise there too (six) and
		// `soundEditorRootDrumActionsMenu` holds the same two `FormattedTitle` sample recorders
		// (one). The shared `source0Menu` / `modulator0Menu` pages are one node visited once, so
		// their own two collisions are not repeated.
		//
		// Batch 3's own repeated names are never *siblings*: `STRING_FOR_ACTIONS` names five
		// different roots' actions pages and `STRING_FOR_MASTER` three, but each is an only child of
		// its own parent. Every one of the sixteen is a legacy `FormattedTitle` -- a name legacy
		// builds by substituting an index, which one `uint16_t Node::name` cannot express, and which
		// E2's formatted-name field removes.
		expect(duplicates).to_equal(size_t(16));
		expect(others).to_equal(size_t(0));
	});

	it("validates the settings root clean everywhere but its follow-channel page", _ {
		// The follow-channel page is sixteen siblings that legacy names identically and formats an
		// index into (`midi/follow/follow_channel.h:142-149`); the `DuplicateName` rule is
		// quantified over *pairs*, so those sixteen alone are 120 reports -- past `validate_tree`'s
		// own 64-issue cap, which would then hide anything else. So the settings tree is validated
		// here with that one page held out, and the page itself in the next example.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		std::vector<const Node*> without_follow_channel;
		const auto add_children_except = [&](const Node& parent, const Node* skip) {
			for (const Node* kid : parent.children) {
				if (kid != skip) {
					without_follow_channel.push_back(kid);
				}
			}
		};
		add_children_except(nodes::root_settings, &nodes::midi_settings);
		add_children_except(nodes::midi_settings, &nodes::midi_follow);
		add_children_except(nodes::midi_follow, &nodes::midi_follow_channel);

		const MenuContext contexts[] = {settings.ctx};
		const auto issues = menu::validate_tree(
		    without_follow_channel, contexts, {}, {},
		    menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));

		// Fix round 1, minor 6: passing those three nodes' *children* as roots leaves the three
		// parents themselves unvalidated -- their own titles, their own emptiness, and sibling-level
		// `DuplicateName` at their level. `validate_tree` cannot be pointed at them without dragging
		// the follow-channel page back in, so the same three rules are asserted directly here.
		for (const Node* parent : {&nodes::root_settings, &nodes::midi_settings, &nodes::midi_follow}) {
			expect(parent->title != uint16_t(0)).to_be_true();     // MissingTitle
			expect(parent->children.empty()).to_be_false();        // EmptySubmenu
			expect(parent->kind == menu::Kind::Submenu).to_be_true();
			for (size_t a = 0; a < parent->children.size(); a++) { // DuplicateName
				for (size_t b = a + 1; b < parent->children.size(); b++) {
					expect(parent->children[a]->name == parent->children[b]->name).to_be_false();
				}
			}
		}
	});

	it("reports the follow-channel page's sixteen identically named siblings", _ {
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		const Node* page[] = {&nodes::midi_follow_channel};
		const MenuContext contexts[] = {settings.ctx};
		const auto issues =
		    menu::validate_tree(page, contexts, {}, {},
		                        menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		// Every issue is the one collision, on the one name id -- the count itself is the 64 cap
		// (`validate.h`: "a tree with more than 64 problems does not need an exhaustive list"), not a
		// number worth asserting. E2's formatted-name field on `Node` is what removes these.
		expect(issues.empty()).to_be_false();
		for (const menu::Issue& i : issues) {
			expect(i.kind == menu::Issue::Kind::DuplicateName).to_be_true();
			expect(i.detail == uint16_t(String::STRING_FOR_FOLLOW_CHANNEL_TRACK)).to_be_true();
		}
	});

	it("assigns an id to every node in every root's tree", _ {
		std::vector<MenuContext> contexts;
		for (const Root& root : roots()) {
			contexts.push_back(root.ctx);
		}
		(void)menu::validate_tree(nodes::kAllRoots, contexts, {}, {},
		                          menu::ValidateOptions{.allow_placeholder_custom = true});
		for (const Pair& p : tree().pairs) {
			expect(menu::node_id(*p.node) != uint16_t(0)).to_be_true();
		}
	});

	it("uses every declared Kind at least three times", _ {
		// Spec E §4's rule, applied with the whole tree in hand (this is the task-8 check `node.h`'s
		// `Kind` comment records the outcome of). A kind below three nodes is speculation, and the
		// four that were -- `Bipolar`, `Range`, `MultiRange`, `Colour` -- plus `Action`, which no
		// table declares either, were removed from the enum rather than left in.
		// Sized from the enum's own last enumerator and swept in full (fix round 1, minor 8): a
		// hand-written list of kinds would let a future enumerator escape the rule silently, and an
		// oversized array would report a phantom zero-count kind.
		constexpr size_t kNumKinds = static_cast<size_t>(menu::Kind::Custom) + 1;
		std::array<size_t, kNumKinds> counts{};
		std::set<const Node*> seen;
		for (const Pair& p : tree().pairs) {
			if (!seen.insert(p.node).second) {
				continue; // a shared node is one node
			}
			counts[static_cast<size_t>(p.node->kind)]++;
		}
		for (size_t i = 0; i < counts.size(); i++) {
			expect(counts[i] >= size_t(3)).to_be_true();
		}
	});

	it("reports a set hook that would swallow the reverb-update flag", _ {
		// The task-8 ruling, checked on a fake node rather than on the real tables (where the
		// combination must never appear): `bindings::write` returns inside its `set` arm before the
		// flag is read, so declaring both is a silent no-op.
		static constexpr menu::Node kBad = [] {
			menu::Node n = menu::param(1, {deluge::modulation::params::Kind::UNPATCHED_SOUND, 0});
			n.bind.set = +[](const MenuContext&, int32_t) {};
			n.bind.updates_reverb_params = true;
			return n;
		}();
		static constexpr const Node* kBadRoots[] = {&kBad};
		const auto issues = menu::validate_tree(kBadRoots, {});
		size_t flagged = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::FlagMismatch
			    && i.detail == static_cast<uint16_t>(menu::Flag::ReverbUpdateWithHook)) {
				flagged++;
			}
		}
		expect(flagged).to_equal(size_t(1));

		// ... and the real tables never do it: the two `UpdatingReverbParams` nodes are pure params.
		size_t reverb_nodes = 0;
		for (const Pair& p : tree().pairs) {
			if (p.node->bind.updates_reverb_params) {
				expect(p.node->bind.set == nullptr).to_be_true();
				reverb_nodes++;
			}
		}
		expect(reverb_nodes > size_t(0)).to_be_true();
	});
});

describe tables_3_writes("scenes::menu batch-3 tables: writes", $ {
	it("leaves the model where the legacy item leaves it, for every set hook", _ {
		// The task-7 ruling, applied to batch 3: every `set` hook is driven for three values
		// spanning its range, each write path run **from its own copy of a common starting state**
		// (`probe_write`), with both readers observed after each.
		//
		// Fix round 1, I2: this used to run legacy first and the port second and assert the port
		// changed nothing -- which a port hook that stores nothing also satisfies, because legacy had
		// already moved the model to `v`. Starting each path from a sentinel that is not `v` is what
		// makes a no-op hook visible.
		size_t exercised = 0;
		std::set<const Node*> covered;
		for (const Pair& p : tree().pairs) {
			if (p.node->bind.set == nullptr || legacy_write_needs_ui(*p.node) || !relevant_here(p)) {
				continue;
			}
			install(p.root->ctx, p.root->song);
			const menu::IntRange r = menu::bindings::range(*p.node, p.root->ctx);
			for (int32_t v : probe_values(r)) {
				const std::optional<int32_t> sentinel = sentinel_for(r, v);
				if (!sentinel.has_value()) {
					continue; // a one-value range has no state to start from that is not `v`
				}
				const WriteProbe probe = probe_write(*p.node, p.item, p.root->ctx, v, *sentinel);
				if (!probe.ran) {
					break; // holds no value at all
				}
				expect(probe.got_port == probe.want_port).to_be_true();
				expect(probe.got_legacy == probe.want_legacy).to_be_true();
				exercised++;
				covered.insert(p.node);
			}
		}
		// 184 distinct `set`-hook nodes, 1152 (node, root, value) probes across the ten roots -- a
		// node reachable from three roots is driven three times, on three different fixtures. A drop
		// in either number means the loop stopped reaching hooks it used to.
		expect(exercised > size_t(1000)).to_be_true();
		expect(covered.size() > size_t(160)).to_be_true();
	});

	it("drives the port hook of every node the legacy write comparison excludes", _ {
		// Fix round 2, I1 + I3. `kLegacyWriteNeedsUi` suppresses the **legacy** call only. Every one
		// of those fourteen port hooks is driven here on its own and its persisted effect asserted
		// directly -- model state, `playbackHandler`, `FlashStorage`, `runtimeFeatureSettings` --
		// because the read comparison does not reach any of it. The `covered` tally at the end is
		// the ratchet, and it holds node identities rather than a count: a fifteenth exclusion with
		// no arm here fails this example, and so does a second `insert` of a node already driven.
		//
		// The six `hid/`-side settings nodes are the ones review round 1's I3 wrongly claimed were
		// already covered ("the spec asserts the persisted half"); they were on no list but this
		// one. They are covered now, in the only half `src/scenes` owns: the `FlashStorage` /
		// `runtimeFeatureSettings` write. Their `hid/` halves stay E2's, as `settings.h` records.
		std::set<const Node*> covered;

		// --- What this example perturbs, and puts back ------------------------------------------
		// These ten globals are process-wide, and the read comparisons in this file's other groups
		// observe some of them. Saved and restored so the example is order-independent.
		const bool saved_midi_out = playbackHandler.midiOutClockEnabled;
		const bool saved_midi_in = playbackHandler.midiInClockEnabled;
		const uint32_t saved_ppqn = playbackHandler.analogOutTicksPPQN;
		const uint8_t saved_brightness = FlashStorage::defaultPadBrightness;
		const MenuHighlighting saved_highlighting = FlashStorage::accessibilityMenuHighlighting;
		const ScreensaverMode saved_screensaver = FlashStorage::screensaverMode;
		const uint8_t saved_timeout = FlashStorage::screensaverTimeoutMinutes;
		const uint32_t saved_sticky = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);
		const uint32_t saved_light_shift = runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed);
		const uint32_t saved_corners = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners);

		{ // --- synth_mode: `Sound::setSynthMode` on the target, not just a stored int ------------
			const Root& sound = installed_root("sound");
			const SynthMode before = sound.ctx.sound->synthMode;
			menu::bindings::write(nodes::synth_mode, sound.ctx, static_cast<int32_t>(SynthMode::RINGMOD));
			expect(sound.ctx.sound->synthMode == SynthMode::RINGMOD).to_be_true();
			menu::bindings::write(nodes::synth_mode, sound.ctx, static_cast<int32_t>(SynthMode::SUBTRACTIVE));
			expect(sound.ctx.sound->synthMode == SynthMode::SUBTRACTIVE).to_be_true();
			sound.ctx.sound->setSynthMode(before, sound.ctx.song);
			covered.insert(&nodes::synth_mode);
		}

		{ // --- arp_mode: the preset re-derivation AND the silence block --------------------------
			// `set_arp_mode` (`nodes/sound_arp.cpp:126-135`) is three effects, not one:
			// `silence_for_arp_toggle`, `arp.mode = mode`, `arp.updatePresetFromCurrentSettings()`.
			// `bindings::read` sees only the middle one, which is why deleting either of the others
			// used to leave this suite green.
			const Root& sound = installed_root("sound");
			ArpeggiatorSettings* arp = sound.ctx.arp;
			expect(arp != nullptr).to_be_true();
			// `silence_for_arp_toggle` only fires on a clip that is actually sounding
			// (`sound_arp.cpp:94`, legacy `mode.h:30`). `song_fixture.h` builds the clip without
			// making it its output's active one -- nothing in the fixture plays -- so this is the
			// one bit of state the example has to arrange for itself.
			expect(sound.ctx.clip != nullptr).to_be_true();
			Clip* const saved_active = OutputPeek::active_clip_of(*sound.ctx.output);
			OutputPeek::active_clip_of(*sound.ctx.output) = sound.ctx.clip;
			expect(sound.ctx.clip->isActiveOnOutput()).to_be_true();

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::DOWN;
			arp->noteMode = ArpNoteMode::DOWN;
			// `Sound::allNotesOff` clears this first thing (`sound.cpp:1761-1763`) -- the one effect
			// of `silence_for_arp_toggle`'s synth arm that survives in model state a spec can read.
			sound.ctx.sound->invertReversed = true;

			menu::bindings::write(nodes::arp_mode, sound.ctx, static_cast<int32_t>(ArpMode::ARP));

			expect(arp->mode == ArpMode::ARP).to_be_true();
			// DOWN/DOWN maps to `ArpPreset::DOWN` (`arpeggiator.cpp:1936-1938`) -- so this is false
			// unless `updatePresetFromCurrentSettings()` actually ran.
			expect(arp->preset == ArpPreset::DOWN).to_be_true();
			// ... and this is true only if `silence_for_arp_toggle` reached the synth arm.
			expect(sound.ctx.sound->invertReversed).to_be_false();
			OutputPeek::active_clip_of(*sound.ctx.output) = saved_active;
			covered.insert(&nodes::arp_mode);
		}

		{ // --- arp_preset: the settings re-derivation, the restart flag, the silence block -------
			const Root& sound = installed_root("sound");
			ArpeggiatorSettings* arp = sound.ctx.arp;
			Clip* const saved_active = OutputPeek::active_clip_of(*sound.ctx.output);
			OutputPeek::active_clip_of(*sound.ctx.output) = sound.ctx.clip;

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::RANDOM;
			arp->noteMode = ArpNoteMode::RANDOM;
			arp->flagForceArpRestart = false;
			sound.ctx.sound->invertReversed = true;

			menu::bindings::write(nodes::arp_preset, sound.ctx, static_cast<int32_t>(ArpPreset::DOWN));

			expect(arp->preset == ArpPreset::DOWN).to_be_true();
			// `updateSettingsFromCurrentPreset()` (`arpeggiator.cpp:1971-2005`) is the only thing
			// that moves these three; delete its call from `set_arp_preset` and all three fail.
			expect(arp->mode == ArpMode::ARP).to_be_true();
			expect(arp->octaveMode == ArpOctaveMode::DOWN).to_be_true();
			expect(arp->noteMode == ArpNoteMode::DOWN).to_be_true();
			expect(arp->flagForceArpRestart).to_be_true();
			expect(sound.ctx.sound->invertReversed).to_be_false();

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->flagForceArpRestart = false;
			OutputPeek::active_clip_of(*sound.ctx.output) = saved_active;
			covered.insert(&nodes::arp_preset);
		}

		{ // --- mod_fx_type: `ModControllableAudio::setModFXType` on the target -------------------
			const Root& sound = installed_root("sound");
			const ModFXType before = sound.ctx.mod->modFXType_;
			menu::bindings::write(nodes::mod_fx_type, sound.ctx, static_cast<int32_t>(ModFXType::NONE));
			expect(sound.ctx.mod->modFXType_ == ModFXType::NONE).to_be_true();
			menu::bindings::write(nodes::mod_fx_type, sound.ctx, static_cast<int32_t>(ModFXType::FLANGER));
			expect(sound.ctx.mod->modFXType_ == ModFXType::FLANGER).to_be_true();
			(void)sound.ctx.mod->setModFXType(before);
			covered.insert(&nodes::mod_fx_type);
		}

		{ // --- audio_clip_reverse: the clip's own sample controls --------------------------------
			const Root& audio = installed_root("audio clip");
			auto* clip = static_cast<AudioClip*>(audio.ctx.clip);
			expect(clip != nullptr).to_be_true();
			const bool before = clip->sampleControls.reversed;
			menu::bindings::write(nodes::audio_clip_reverse, audio.ctx, 1);
			expect(clip->sampleControls.reversed).to_be_true();
			menu::bindings::write(nodes::audio_clip_reverse, audio.ctx, 0);
			expect(clip->sampleControls.reversed).to_be_false();
			clip->sampleControls.reversed = before;
			covered.insert(&nodes::audio_clip_reverse);
		}

		// --- The four `playbackHandler` / settings-root nodes ------------------------------------
		// These hooks ignore the context entirely (`settings.cpp`'s signatures take an unnamed
		// `const MenuContext&`), so the settings root's own context is the honest one to pass.
		const Root& settings = installed_root("settings");

		{ // midi_clock_out -- `setMidiOutClockMode` (the MIDI send is the part legacy cannot do here)
			menu::bindings::write(nodes::midi_clock_out, settings.ctx, 1);
			expect(playbackHandler.midiOutClockEnabled).to_be_true();
			menu::bindings::write(nodes::midi_clock_out, settings.ctx, 0);
			expect(playbackHandler.midiOutClockEnabled).to_be_false();
			covered.insert(&nodes::midi_clock_out);
		}
		{ // midi_clock_in
			menu::bindings::write(nodes::midi_clock_in, settings.ctx, 1);
			expect(playbackHandler.midiInClockEnabled).to_be_true();
			menu::bindings::write(nodes::midi_clock_in, settings.ctx, 0);
			expect(playbackHandler.midiInClockEnabled).to_be_false();
			covered.insert(&nodes::midi_clock_in);
		}
		{ // trigger_out_ppqn
			menu::bindings::write(nodes::trigger_out_ppqn, settings.ctx, 24);
			expect(playbackHandler.analogOutTicksPPQN).to_equal(uint32_t(24));
			menu::bindings::write(nodes::trigger_out_ppqn, settings.ctx, 48);
			expect(playbackHandler.analogOutTicksPPQN).to_equal(uint32_t(48));
			covered.insert(&nodes::trigger_out_ppqn);
		}
		{ // default_pad_brightness -- the `>> 2` the node's step-of-4 range implies
			menu::bindings::write(nodes::default_pad_brightness, settings.ctx, 32);
			expect(FlashStorage::defaultPadBrightness).to_equal(uint8_t(8));
			menu::bindings::write(nodes::default_pad_brightness, settings.ctx, 64);
			expect(FlashStorage::defaultPadBrightness).to_equal(uint8_t(16));
			expect(menu::bindings::read(nodes::default_pad_brightness, settings.ctx)).to_equal(int32_t(64));
			covered.insert(&nodes::default_pad_brightness);
		}
		{ // default_menu_highlighting
			menu::bindings::write(nodes::default_menu_highlighting, settings.ctx,
			                      static_cast<int32_t>(MenuHighlighting::PARTIAL_INVERSION));
			expect(FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::PARTIAL_INVERSION).to_be_true();
			menu::bindings::write(nodes::default_menu_highlighting, settings.ctx,
			                      static_cast<int32_t>(MenuHighlighting::NO_INVERSION));
			expect(FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION).to_be_true();
			covered.insert(&nodes::default_menu_highlighting);
		}
		{ // screensaver_mode
			menu::bindings::write(nodes::screensaver_mode, settings.ctx, static_cast<int32_t>(ScreensaverMode::OFF));
			expect(FlashStorage::screensaverMode == ScreensaverMode::OFF).to_be_true();
			menu::bindings::write(nodes::screensaver_mode, settings.ctx, static_cast<int32_t>(ScreensaverMode::STARSCAPE));
			expect(FlashStorage::screensaverMode == ScreensaverMode::STARSCAPE).to_be_true();
			covered.insert(&nodes::screensaver_mode);
		}
		{ // screensaver_timeout
			menu::bindings::write(nodes::screensaver_timeout, settings.ctx, 7);
			expect(FlashStorage::screensaverTimeoutMinutes).to_equal(uint8_t(7));
			menu::bindings::write(nodes::screensaver_timeout, settings.ctx, 21);
			expect(FlashStorage::screensaverTimeoutMinutes).to_equal(uint8_t(21));
			covered.insert(&nodes::screensaver_timeout);
		}
		{ // feature_sticky_shift -- plus the LightShiftLed poke `ShiftIsSticky` carries with it
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed,
			                           util::to_underlying(RuntimeFeatureStateToggle::Off));
			menu::bindings::write(nodes::feature_sticky_shift, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::On));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::On));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::On));
			menu::bindings::write(nodes::feature_sticky_shift, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::Off));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::Off));
			covered.insert(&nodes::feature_sticky_shift);
		}
		{ // feature_rounded_corners
			menu::bindings::write(nodes::feature_rounded_corners, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::On));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::On));
			menu::bindings::write(nodes::feature_rounded_corners, settings.ctx,
			                      util::to_underlying(RuntimeFeatureStateToggle::Off));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners))
			    .to_equal(util::to_underlying(RuntimeFeatureStateToggle::Off));
			covered.insert(&nodes::feature_rounded_corners);
		}

		// The ratchet.
		expect(covered).to_equal(std::set<const Node*>(std::begin(kLegacyWriteNeedsUi), std::end(kLegacyWriteNeedsUi)));

		playbackHandler.midiOutClockEnabled = saved_midi_out;
		playbackHandler.midiInClockEnabled = saved_midi_in;
		playbackHandler.analogOutTicksPPQN = saved_ppqn;
		FlashStorage::defaultPadBrightness = saved_brightness;
		FlashStorage::accessibilityMenuHighlighting = saved_highlighting;
		FlashStorage::screensaverMode = saved_screensaver;
		FlashStorage::screensaverTimeoutMinutes = saved_timeout;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, saved_sticky);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, saved_light_shift);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, saved_corners);
	});

	it("round-trips every param node, and raises the reverb flag on exactly the two that should", _ {
		size_t exercised = 0;
		size_t flagged = 0;
		for (const Pair& p : tree().pairs) {
			if (!p.node->bind.param.valid() || p.node->bind.set != nullptr || p.node->kind == menu::Kind::Custom
			    || !relevant_here(p)) {
				continue;
			}
			// A param node is only round-trippable where the context can actually serve it; the
			// settings root has no param manager at all.
			if (p.root->ctx.params == nullptr) {
				continue;
			}
			install(p.root->ctx, p.root->song);
			const menu::IntRange r = menu::bindings::range(*p.node, p.root->ctx);
			for (int32_t v : probe_values(r)) {
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
				menu::bindings::write(*p.node, p.root->ctx, v);
				// Fix round 2: this used to be `if (read != v) continue;` -- an escape hatch that
				// excused any param write that did nothing, silently, including a real regression.
				// It is unnecessary: `relevant_here` above already restricts the loop to nodes their
				// own root's context shows, and a relevant param node is servable by construction
				// (that is what `serves_for` decides). So the round trip is asserted outright.
				expect(menu::bindings::read(*p.node, p.root->ctx)).to_equal(v);
				expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender)
				    .to_equal(p.node->bind.updates_reverb_params);
				exercised++;
			}
		}
		expect(exercised > size_t(400)).to_be_true(); // 462 today

		std::set<const Node*> reverb;
		for (const Pair& p : tree().pairs) {
			if (p.node->bind.updates_reverb_params) {
				reverb.insert(p.node);
			}
		}
		flagged = reverb.size();
		// `sidechain::Shape` (batch 2) and `sidechain::GlobalVolume` (this batch) -- the only two
		// `unpatched_param::UpdatingReverbParams` subclasses in the whole legacy tree.
		expect(flagged).to_equal(size_t(2));
		expect(reverb.contains(&nodes::sidechain_shape)).to_be_true();
		expect(reverb.contains(&nodes::global_sidechain_volume)).to_be_true();
	});
});

describe tables_3_chaining("scenes::menu batch-3 tables: selectButtonPress chaining", $ {
	it("answers what cv::Selection and gate::Selection answer, output by output", _ {
		// `cv/selection.h:41-46` and `gate/selection.h:43-54`, compared against the live legacy
		// globals rather than a hand-written expectation. Both selectors read
		// `soundEditor.currentSourceIndex` -- the port's `MenuContext::source_index` -- so the
		// sweep is over every value legacy distinguishes, the `NUM_GATE_CHANNELS` sentinel included.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);

		for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(which);
			soundEditor.currentSourceIndex = which;

			cvSelectionMenu.setValue(which);
			gateSelectionMenu.setValue(which);
			expect(cvSelectionMenu.selectButtonPress() == &cvSubmenu).to_be_true();
			expect(nodes::cv_selection.select_chain(ctx, menu_scene().state()) == &nodes::cv_submenu).to_be_true();

			::MenuItem* legacy_gate = gateSelectionMenu.selectButtonPress();
			const Node* want_gate = legacy_gate == &gateOffTimeMenu    ? &nodes::gate_off_time
			                        : legacy_gate == &gateModeMenu     ? &nodes::gate_mode
			                                                           : nullptr;
			expect(want_gate != nullptr).to_be_true(); // legacy always names one of the two
			expect(nodes::gate_selection.select_chain(ctx, menu_scene().state()) == want_gate).to_be_true();
		}
		soundEditor.currentSourceIndex = settings.ctx.source_index;
	});

	it("declares the three chain-only pages with the legacy items' own shape and values", _ {
		// `cvSubmenu` (`menus.cpp:1232`), `gateModeMenu` (`:1223`) and `gateOffTimeMenu` (`:1224`)
		// are children of nothing, so the lockstep walk above can never see them. Their shape and
		// their reads are compared here instead, against the same live objects.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);

		const auto& cv_kids = SubmenuPeek::items_of(cvSubmenu);
		expect(nodes::cv_submenu.children.size()).to_equal(cv_kids.size());
		expect(nodes::cv_submenu.children.size()).to_equal(size_t(2));
		expect(nodes::cv_submenu.kind == menu::Kind::Submenu).to_be_true();
		for (size_t i = 0; i < cv_kids.size(); i++) {
			expect(text_of(nodes::cv_submenu.children[i]->name)).to_equal(std::string{cv_kids[i]->getName()});
		}

		// Values, on each CV output in turn -- `cv::Volts` and `cv::Transpose` both index
		// `cvEngine.cvChannels[currentSourceIndex]`, so a port that ignored `source_index` would
		// pass on output 0 alone.
		for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(which);
			soundEditor.currentSourceIndex = which;

			cvVoltsMenu.readCurrentValue();
			expect(bindings::read(nodes::cv_volts, ctx)).to_equal(cvVoltsMenu.getValue());
			cvTransposeMenu.readCurrentValue();
			expect(bindings::read(nodes::cv_transpose, ctx)).to_equal(cvTransposeMenu.getValue());

			// A write goes to that output and no other.
			bindings::write(nodes::cv_volts, ctx, 100 + which);
			expect(int32_t(cvEngine.cvChannels[which].voltsPerOctave)).to_equal(100 + which);
		}

		for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
			MenuContext ctx = settings.ctx;
			ctx.source_index = static_cast<int8_t>(which);
			soundEditor.currentSourceIndex = which;

			gateModeMenu.readCurrentValue();
			expect(bindings::read(nodes::gate_mode, ctx)).to_equal(gateModeMenu.getValue());

			// `gate::Mode::updateOptions` (`gate/mode.h:53-72`): the two trigger polarities always,
			// plus CLOCK or RUN SIGNAL on the two outputs that carry them.
			gateModeMenu.updateOptions(which);
			const auto legacy_options = gateModeMenu.getOptions(legacy::Selection::OptType::FULL);
			std::array<std::string_view, menu::kMaxDynamicNames> names{};
			const size_t n = nodes::gate_mode.bind.dynamic_names(ctx, std::span<std::string_view>{names});
			expect(n).to_equal(legacy_options.size());
			for (size_t i = 0; i < n && i < legacy_options.size(); i++) {
				expect(std::string{names[i]}).to_equal(std::string{legacy_options[i]});
			}
		}
		soundEditor.currentSourceIndex = settings.ctx.source_index;

		gateOffTimeMenu.readCurrentValue();
		expect(bindings::read(nodes::gate_off_time, settings.ctx)).to_equal(gateOffTimeMenu.getValue());
		expect(nodes::gate_off_time.bind.decimal_places).to_equal(uint8_t(1)); // `off_time.h:25`
		expect(nodes::gate_off_time.bind.fixed_range.has_value()).to_be_true();
		expect(nodes::gate_off_time.bind.fixed_range->min).to_equal(int32_t(1));  // `:23`
		expect(nodes::gate_off_time.bind.fixed_range->max).to_equal(int32_t(100)); // `:24`
	});

	it("numbers the three chain-only settings pages, which no children span reaches", _ {
		// The reachability ruling, on the settings half: `assign_node_ids` follows `chain_targets`,
		// so `SceneId::arg` can name one of these pages and `MenuScene::on_enter` can restore it.
		(void)menu::assign_node_ids(nodes::kAllRoots);
		for (const Node* n : {&nodes::cv_submenu, &nodes::cv_volts, &nodes::cv_transpose, &nodes::gate_mode,
		                      &nodes::gate_off_time}) {
			const uint16_t id = menu::node_id(*n);
			expect(id != uint16_t(0)).to_be_true();
			expect(menu::node_by_id(id) == n).to_be_true();
		}
		// And they really are chain-only: the settings root's thirteen children do not name them,
		// and neither does anything else's `children`.
		for (const Node* n : {&nodes::cv_submenu, &nodes::gate_mode, &nodes::gate_off_time}) {
			expect(reachable_by_children(n)).to_be_false();
		}
		// `cv_volts` / `cv_transpose` are ordinary children -- of a page that is itself chain-only.
		expect(reachable_by_children(&nodes::cv_volts)).to_be_true();
	});

	it("validates the three chain-only pages along with the settings root", _ {
		// They are only in the walk because `collect` follows `chain_targets` too; without that the
		// `Unreachable` rule would fire on every one of them (`assign_node_ids` would have numbered
		// them while this walk did not, or the reverse).
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		const Node* subtree[] = {&nodes::cv_selection, &nodes::gate_selection};
		const MenuContext contexts[] = {settings.ctx};
		const auto issues =
		    menu::validate_tree(subtree, contexts, {}, {},
		                        menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));
	});
});

describe tables_3_pixels("scenes::menu batch-3 tables: pixels", $ {
	// Shared setup, so a pixel test added here later gets it for free rather than needing to
	// remember to copy it in (review round 1, I4 -- the two tests below used to each inline this).
	// `tables_3_writes` above drives every set hook across the whole tree, including params an
	// alpha/beta build's `bindings::write` cannot serve in the probe context it uses -- which raises
	// a real `Kind::Console` "MENU PARAM UNBOUND" notice (`bindings.cpp`'s `notice_unservable_param`).
	// That notice sat harmlessly undrained in the global queue until `shell/notices.cpp`'s
	// `Kind::Console` fix (task 11) made a drained Console notice actually composite into whatever
	// next calls `shell.tick()` -- one of the tests below, in the same process. Drain both global
	// queues before each `it` here so a stray fact from an earlier describe block, or from the
	// *other* test in this one, never bleeds into a golden.
	before_each([] {
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		deluge::notify::dirty().drain();
		deluge::notify::Notice stray;
		while (deluge::notify::notices().pop(stray)) {}
	});

	// The golden names are `menu_settings_tree_*`, not `menu_settings_root_*`: those belong to
	// `menu_scene_spec`, whose own `settings_root` is a hand-built three-item tree used to exercise
	// the interpreter. This one is the real thirteen-item settings root.
	it("renders the settings root list", _ {
		const Root& settings = roots().back();
		Rig r;
		r.open(nodes::root_settings, settings.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/menu_settings_tree_48.pbm", r.fake.pbm(0));
	});

	it("renders the settings root list on a 64-row panel", _ {
		const Root& settings = roots().back();
		Rig r{board_64(), profile_64()};
		r.open(nodes::root_settings, settings.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/menu_settings_tree_64.pbm", r.fake.pbm(0));
	});
});
// clang-format on

/// Six `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`.
int tables_3_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(tables_3_shape, tables_3_values, tables_3_validation, tables_3_writes, tables_3_chaining,
	                          tables_3_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
