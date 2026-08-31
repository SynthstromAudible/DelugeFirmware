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

#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/dynamic.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/context_menu/reset_settings.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"

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

#include "gui/menu_item/midi/devices.h"

#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"

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
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
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
/// (`menus.cpp:1769`), which holds both sample recorders, is a child of **`soundEditorRootMenuMidiDrum`
/// and `soundEditorRootMenuGateDrum`** (`:1969`, `:1977`), whose `setup()` arm leaves
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
/// been. `defaults::Velocity::writeCurrentValue` (`defaults/velocity.h:29-32`) is `FlashStorage`
/// plus `currentSong->setDefaultVelocityForAllInstruments` -- pure model, over the very song
/// `install()` points `currentSong` at, and the one place the port's `ctx.song` and legacy's
/// `currentSong` can be *seen* to agree. `trigger::in::PPQN`'s resync is guarded by
/// `isExternalClockActive() && usingAnalogClockInput` (`trigger/in/ppqn.h:24-26`), both false in
/// this fixture. Both are driven now.
constexpr const Node* kLegacyWriteNeedsUi[] = {
    &nodes::synth_mode,                // `view.setKnobIndicatorLevels()` (`synth_mode.h:57`)
    &nodes::arp_mode,                  // `allNotesOff` over `soundEditor.getCurrentModelStack`
    &nodes::arp_preset,                // the same block (`preset_mode.h:50-70`)
    &nodes::mod_fx_type,               // `display->displayError` (`mod_fx/type.h:53 writeCurrentValue`, `:59`)
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

/// @brief The one node whose value does **not** live in shared storage, so the value comparisons
///        below have nothing to compare and would fail on the divergence by construction.
///
/// `shortcuts_version` is the exception among E2 task 11's nine items. The other eight address a
/// byte both sides read -- `FlashStorage::padColours` for the six colours, `PadLEDs::flashCursor`
/// for the play cursor, `runtimeFeatureSettings` for dev sysex -- so the generic loops cross-check
/// them for free. This one cannot: legacy keeps the setting on `soundEditor`
/// (`shortcuts/version.h:26-27`) and `src/scenes` may not name `soundEditor`, so the port keeps it
/// in `menu/shortcuts.cpp` beside the tables it selects. Two independent copies read back
/// independently, which is exactly what these loops are built to catch, so it is excluded here and
/// proved on its own terms in the batch-4 group: the round trip, and -- the thing that actually
/// matters -- that the layer it selects matches the one legacy's `setShortcutsVersion` produces,
/// cell for cell.
bool value_is_port_owned(const Node& node) {
	return &node == &nodes::shortcuts_version;
}

/// @brief The three nodes whose value is a **pair**, so the one-`int32_t` loops below have nothing
///        to compare.
///
/// `default_tempo`, `default_swing_amount` and `default_key` are `Kind::Range` (E2 task 11b): their
/// `lower` and `upper` live in `FlashStorage::defaultRanges` and legacy edits both of them in
/// `IntegerRange`/`KeyRange::selectEncoderAction`. Neither legacy class has a `writeCurrentValue` at
/// all -- the members *are* the storage -- so `legacy_value` reads `Value<int32_t>::value_`, a field
/// nothing on either side ever writes, and `probe_write` drives a legacy path that stores nothing.
/// Including them here would compare two zeroes and pass whatever the port did.
///
/// @note **This is an exclusion from these two loops, not from comparison.**
///       `sim/scenes_spec/menu_ranges_spec.cpp` compares all three against the real legacy objects
///       far more thoroughly than a single-value probe could: the spellings over every value in
///       range, both clamp rules, the edge selection, the `beginSession` seeding and the rendered
///       pixels -- each with legacy and the port driven independently from the same sentinel.
bool value_is_two_ended(const Node& node) {
	return node.kind == menu::Kind::Range;
}

/// @brief The one `Kind::Enum` whose option list is **not** reproducible: its ON option names a
///        randomly generated key.
///
/// `DevSysexSetting::getOptions` renders `onValue`'s eight hex digits
/// (`runtime_feature/devSysexSetting.cpp:52-59`), and `onValue` is drawn from `getNoise()` whenever
/// the setting is off (`:36-40`). The port draws its own from the same LCG, so with nothing stored
/// the two strings differ by design and comparing them would be comparing two random numbers.
///
/// @note **This is an exclusion from the sweep, not from comparison.** Both sides adopt a *stored*
///       key when there is one (`:33-35`), so the batch-4 group's
///       `formats the dev sysex key exactly as legacy's own intToHex does` stores one first and then
///       compares the two option lists byte for byte, over five keys chosen to exercise every branch
///       of `halfByteToHexChar`. That example is what holds the port's digit table to
///       `util/c_string.cpp`'s; this predicate only keeps the *unstored* case out of a loop that has
///       no way to seed it.
bool names_are_random(const Node& node) {
	return &node == &nodes::feature_dev_sysex;
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

/// @brief The legacy item the lockstep walk paired @p node with, or null if it reaches no root.
///
/// The pairing is `walk`'s, not a hand-written table: an example that names a legacy global directly
/// would be asserting against its *own* idea of which item a node replaces, which is the thing under
/// test. Several of the items the batch-4 group needs (`flashStatusMenu`, `shortcutsVersionMenu`,
/// `menuDevSysexAllowed`) are file-scope in `menus.cpp` / `runtime_feature/settings.cpp` and named in
/// no header, so this is also the only way to reach them without a bare `extern` per item.
::MenuItem* legacy_item_for(const Node& node);

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

::MenuItem* legacy_item_for(const Node& node) {
	for (const Pair& p : tree().pairs) {
		if (p.node == &node) {
			return p.item;
		}
	}
	return nullptr;
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

/// Board encoder index (`kDelugeEncoders`, `shell/profiles/deluge.cpp`): 3 = Select. Matrix
/// coordinates (`kMatrix`, same file) for the select-encoder button. Same spelling every other
/// spec in this suite uses.
constexpr uint8_t kSelectEncoderIndex = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief Detaches `$DELUGE_SD_ROOT` for the scope, and restores it after.
///
/// `RuntimeFeatureSettings::factoryReset` (`model/settings/runtime_feature_settings.cpp:176-187`,
/// the port's `accept_reset_community_features`'s own call) `unlink`s `CommunityFeatures.XML` --
/// and on the host that is a real `::unlink` under `$DELUGE_SD_ROOT`
/// (`src/bsp/host/host_efatfs_passthrough.cpp`). The aggregate `scenes_spec` ctest row points that
/// at `tests/fixtures/card`; unset, every filesystem call returns "no such file", the path both
/// `factoryReset` and this same guard in `context_menu_scene_spec.cpp`'s `NoSdRoot` already treat
/// as harmless. Duplicated here (file-local, like that one) rather than shared, for the same
/// reason every other helper in this file is.
struct NoSdRoot {
	std::string saved;
	bool had;
	NoSdRoot() {
		const char* root = std::getenv("DELUGE_SD_ROOT");
		had = root != nullptr;
		if (had) {
			saved = root;
		}
		(void)::unsetenv("DELUGE_SD_ROOT");
	}
	~NoSdRoot() {
		if (had) {
			(void)::setenv("DELUGE_SD_ROOT", saved.c_str(), 1);
		}
	}
	NoSdRoot(const NoSdRoot&) = delete;
	NoSdRoot& operator=(const NoSdRoot&) = delete;
};

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

	/// @brief One button press/release at matrix (\p x, \p y) -- the select-encoder press an accept
	///        or a descent rides on, the same coordinates every other spec in this suite uses.
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One detent of the select encoder (2 edges/detent, `LoweringConfig::edges_per_detent`).
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoderIndex, 0, static_cast<int16_t>(detents * 2)}, at_ms);
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

/// @name The twelve MIDI-command learn items' own support.
///
/// The port's items are `nodes::MidiCommandItem`s (`menu/nodes/midi_command.h`); the legacy items
/// they replace are the twelve `midi::Command` globals under `midiCommandsMenu`
/// (`gui/ui/menus.cpp:1416-1436`), which this spec reaches through `tree()` rather than through an
/// `extern` -- `menus.h` declares none of them, and reaching them by walking the shipped tree is
/// what makes "same position, same command" checkable at all.
/// @{

/// @brief A concrete `MIDICable` to learn against.
///
/// `MIDICable` is abstract (`io/midi/midi_device.h:96-167` -- six pure virtuals), and none of the
/// shipped cable types can be built in a host spec without a USB or DIN backend behind it. Only the
/// cable's *address* and its display name are ever read by the code under test
/// (`LearnedMIDI::cable`, and `MidiCommandItem::render`'s device row), so a stub is the whole
/// requirement.
struct FakeCable final : ::MIDICable {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "SPEC CABLE"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

FakeCable& fake_cable() {
	static FakeCable c;
	return c;
}

/// @brief Every `midiEngine.globalMIDICommands` entry, saved so a learn example can put them back.
///
/// The whole array, not just the slot under test: an item that learned onto the *wrong* command is
/// exactly the bug the twelve-way mapping has to catch, and a save/restore of one slot would leak
/// the wrong-slot write into the next example instead of failing this one.
struct CommandSlots {
	LearnedMIDI slots[kNumGlobalMIDICommands];

	static CommandSlots capture() {
		CommandSlots c;
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			c.slots[i] = midiEngine.globalMIDICommands[i];
		}
		return c;
	}
	void restore() const {
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			midiEngine.globalMIDICommands[i] = slots[i];
		}
	}
	static void clear_all() {
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			midiEngine.globalMIDICommands[i].clear();
		}
	}
	/// @brief The whole array as one comparable string: `index=cable/channelOrZone/noteOrCC`, with
	///        the empty slots elided. Printing the difference beats thirteen separate expectations.
	std::string describe() const {
		std::string out;
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			const LearnedMIDI& s = slots[i];
			if (s.channelOrZone == MIDI_CHANNEL_NONE) {
				continue; // `LearnedMIDI::containsSomething()`, which is not const
			}
			out += std::to_string(i) + "=" + (s.cable != nullptr ? s.cable->getDisplayName() : "none") + "/"
			       + std::to_string(s.channelOrZone) + "/" + std::to_string(s.noteOrCC) + " ";
		}
		return out.empty() ? std::string{"(all clear)"} : out;
	}
};

/// @brief One of the twelve: the node, its `MidiCommandItem`, and the legacy `midi::Command` the
///        tree walk found at the very same position.
struct CommandPair {
	const Node* node;
	const nodes::MidiCommandItem* port;
	legacy::midi::Command* legacy;
};

/// @brief `nodes::kMidiCommandsChildren` paired with `midiCommandsMenu`'s own children.
///
/// Built off `tree()`, so the pairing is positional in the *shipped* trees rather than a second
/// hand-written list that could drift from either one.
const std::vector<CommandPair>& command_pairs() {
	static std::vector<CommandPair> list = [] {
		std::vector<CommandPair> v;
		for (const Node* node : nodes::kMidiCommandsChildren) {
			CommandPair p{node, dynamic_cast<const nodes::MidiCommandItem*>(node->custom), nullptr};
			for (const Pair& pair : tree().pairs) {
				if (pair.node == node) {
					p.legacy = dynamic_cast<legacy::midi::Command*>(pair.item);
					break;
				}
			}
			v.push_back(p);
		}
		return v;
	}();
	return list;
}

/// @brief Records `display->displayPopup(...)`, so the legacy item can be driven at all.
///
/// `display` is null in this process (`hid/display/display.cpp:4`; nothing in a host spec boots the
/// OLED), and two of `midi::Command`'s arms call straight through it: the "LEARNED"/"UNLEARNED"
/// popup taken when the item is *not* the current menu item (`command.cpp:120-122`, `:100-103`) and
/// FILL's `CANT_LEARN_PC` refusal (`:107`). Without a display the first is avoidable -- point
/// `soundEditor`'s navigation record at the item and the `renderUIsForOled()` branch is taken
/// instead, and that function is one assignment (`gui/ui/ui.cpp:263-265`) -- but the second is not:
/// legacy pops unconditionally there. So the examples below install this for the scope, which both
/// keeps the legacy call alive and makes "did legacy refuse?" directly observable.
struct SpyDisplay final : deluge::hid::Display {
	std::vector<std::string> popups;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override {
		popups.emplace_back(text != nullptr ? text : "");
	}
	void popupText(char const* text, PopupType) override { popups.emplace_back(text != nullptr ? text : ""); }
	void popupTextTemporary(char const* text, PopupType) override { popups.emplace_back(text != nullptr ? text : ""); }
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return !popups.empty(); }
	bool hasPopupOfType(PopupType) override { return !popups.empty(); }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief Installs a `SpyDisplay` and points `soundEditor`'s navigation record at @p item, so the
///        legacy `midi::Command` can be called without dereferencing a null `display`.
///
/// Restores both on the way out: `display` is a process global and `menuItemNavigationRecord` is
/// what `install()` leaves at depth 0, so leaking either would change what a later example sees.
struct LegacyLearnScope {
	SpyDisplay spy;
	deluge::hid::Display* saved_display;
	::MenuItem* saved_item;
	uint8_t saved_depth;

	explicit LegacyLearnScope(::MenuItem* item) : saved_display(display) {
		saved_depth = soundEditor.navigationDepth;
		saved_item = soundEditor.menuItemNavigationRecord[0];
		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = item;
		display = &spy;
	}
	~LegacyLearnScope() {
		display = saved_display;
		soundEditor.menuItemNavigationRecord[0] = saved_item;
		soundEditor.navigationDepth = saved_depth;
	}
	LegacyLearnScope(const LegacyLearnScope&) = delete;
	LegacyLearnScope& operator=(const LegacyLearnScope&) = delete;
};

/// @brief Descend `Rig` @p r from the MIDI-commands submenu onto its @p row'th child.
///
/// The real descent the reset-item examples use: cursor down @p row times, then the select-encoder
/// press `enter_child` -> `enter_leaf` runs on.
void focus_command_row(Rig& r, const MenuContext& ctx, size_t row) {
	// Both global queues first. An undrained `Kind::Text` notice -- a `CANT_LEARN_PC` refusal from a
	// previous iteration, or a stray fact from an earlier describe block in this same process --
	// becomes a `PopupScene` on this Rig's very first tick, which is not `oled_transparent` for
	// `Main` and takes the input top with it (`shell/notices.cpp:118-120`). The select press below
	// would then never reach the menu, and the descent would silently not happen.
	// `tables_3_reset_items` and `tables_3_pixels` drain for exactly this reason.
	deluge::notify::dirty().drain();
	{
		deluge::notify::Notice stray;
		while (deluge::notify::notices().pop(stray)) {}
	}
	r.open(nodes::midi_commands, ctx);
	r.shell.tick(0);
	for (size_t t = 0; t < row; t++) {
		r.turn(1, static_cast<uint32_t>(10 + t * 10));
		r.shell.tick(static_cast<uint32_t>(15 + t * 10));
	}
	r.button(kSelectEncButtonX, kSelectEncButtonY, true, 200);
	r.shell.tick(210);
}
/// @}

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
		// `menus.cpp`'s `settingsRootMenu` literal (`:2152-2168`) -- thirteen children, asserted
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
			if (p.node->kind == menu::Kind::Custom || !relevant_here(p) || value_is_port_owned(*p.node)
			    || value_is_two_ended(*p.node)) {
				continue; // an E2 placeholder, an item this context does not show, a port-owned value,
				          // or a two-ended range (see `value_is_two_ended`)
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
			if (p.node->kind != menu::Kind::Enum || !relevant_here(p) || names_are_random(*p.node)) {
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
		// *parent*, and `soundEditorRootMenuDrum` (`menus.cpp:1807`) is a second parent over the same
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
			if (p.node->bind.set == nullptr || legacy_write_needs_ui(*p.node) || !relevant_here(p)
			    || value_is_port_owned(*p.node) || value_is_two_ended(*p.node)) {
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

	it("never lets a select_chain hook name a node outside its own chain_targets, anywhere in the tree", _ {
		// The containment half of the chain contract, whole-tree. `validate_tree`'s
		// `Flag::ChainTargetsMismatch` can only test both-set-or-neither: a `SelectChainFn` is
		// opaque to a static walk, so nothing structural can prove `range(select_chain)` is a subset
		// of `chain_targets`. A hook that escaped its span would name a page with no node id --
		// unaddressable by `SceneId::arg`, so `on_enter` could never restore it, absent from
		// `validate_tree`'s walk and from the dump -- and every one of those failures is silent.
		//
		// Not provable, but exhaustively *exercised*: every chaining node in `kAllRoots` is asked
		// under every one of the ten root fixtures' contexts (plus the CV one), which between them
		// vary kit-row-ness, output type and `source_index` -- the three facts the shipped hooks
		// actually branch on. Cheap enough to be whole-tree rather than per-example.
		std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
		std::set<const Node*> seen;
		std::vector<MenuContext> contexts;
		for (const Root& root : roots()) {
			contexts.push_back(root.ctx);
		}
		contexts.push_back(cv_fixture().ctx);
		// `gate_selection_chain` is the one hook that branches on `source_index`, and no fixture
		// carries the `NUM_GATE_CHANNELS` sentinel; add both sides of that branch explicitly rather
		// than leave the interesting value unswept.
		for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
			MenuContext ctx = roots().back().ctx;
			ctx.source_index = static_cast<int8_t>(which);
			contexts.push_back(ctx);
		}

		size_t chaining_nodes = 0;
		size_t answers = 0;
		while (!stack.empty()) {
			const Node* n = stack.back();
			stack.pop_back();
			if (n == nullptr || !seen.insert(n).second) {
				continue;
			}
			for (const Node* c : n->children) {
				stack.push_back(c);
			}
			for (const Node* t : n->chain_targets) {
				stack.push_back(t);
			}
			if (n->select_chain == nullptr) {
				continue;
			}
			chaining_nodes++;
			for (const MenuContext& ctx : contexts) {
				const Node* got = n->select_chain(ctx, menu_scene().state());
				if (got == nullptr) {
					continue; // "ascend" -- always legal, and names no page
				}
				answers++;
				const bool contained =
				    std::find(n->chain_targets.begin(), n->chain_targets.end(), got) != n->chain_targets.end();
				// Verdict on the left so a failure reads "expected in / got ESCAPED", and names the
				// page that escaped rather than just reporting a false boolean.
				expect(std::string{contained ? "in chain_targets: " : "ESCAPED chain_targets: "} + text_of(got->name))
				    .to_equal(std::string{"in chain_targets: "} + text_of(got->name));
				// And the consequence the containment exists to guarantee.
				expect(menu::node_id(*got) != uint16_t(0)).to_be_true();
			}
		}
		// The chaining nodes of the shipped tree; a chain added without a spec fails here.
		//
		// **68 after E2 task 9**, up from task 3's seven: 53 `PatchedParam` nodes
		// (`with_patch_chain`, every node with `bind.param.kind == PATCHED`), the four patch-cable
		// pages (`source_selection_{regular,range}`, `patch_cable_strength_{regular,range}`), the
		// three items that open one (`patch_cables`, `vibrato`, `sidechain_volume`), and the range
		// picker. 7 + 53 + 4 + 3 + 1 = 68.
		expect(chaining_nodes).to_equal(size_t(68));
		expect(answers > size_t(0)).to_be_true();
	});

	it("has exactly one chain edge that closes a cycle, and it is the legacy one", _ {
		// The guard the `"ref"` stub cost us. Before E2 task 9 the dump's `fail_on_chain_cycle`
		// *stopped* on a cycle, so an unintended one could not be committed; now it is emitted as a
		// `{"ref": true}` stub and the only remaining net is "the JSON changed", which says nothing
		// about why. This names the edge instead.
		//
		// A **path-aware** walk, not the seen-set one above: a cycle is a target that is already
		// open on the current path, which is exactly `menu_dump_main.cpp`'s `g_path` test and
		// therefore exactly what makes the dump emit a stub.
		// A *set* of edges: the same edge closes a cycle on every path that reaches it (the dump
		// emits a stub at each such occurrence -- 118 of them), and what is being pinned here is
		// which edges can do it at all, not how many times the walk meets one.
		std::set<std::pair<const Node*, const Node*>> back_edges;
		std::vector<const Node*> path;
		std::set<std::pair<const Node*, const Node*>> visited_edges;
		const auto walk = [&](auto&& self, const Node* n) -> void {
			if (n == nullptr) {
				return;
			}
			path.push_back(n);
			for (const Node* c : n->children) {
				if (c != nullptr && visited_edges.insert({n, c}).second) {
					self(self, c);
				}
			}
			for (const Node* t : n->chain_targets) {
				if (t == nullptr) {
					continue;
				}
				if (std::find(path.begin(), path.end(), t) != path.end()) {
					back_edges.insert({n, t});
					continue; // the dump stops here too -- it emits the stub and does not recurse
				}
				if (visited_edges.insert({n, t}).second) {
					self(self, t);
				}
			}
			path.pop_back();
		};
		for (const Node* root : nodes::kAllRoots) {
			walk(walk, root);
		}

		// **Both directions of one two-node loop**, and nothing else.
		// `patch_cable_strength::rangeMenu`'s select press opens `source_selection::rangeMenu`
		// (`patch_cable_strength.cpp:302-309`) and that page's opens the strength page straight back
		// (`source_selection/range.cpp:32-34`) -- legacy's own loop, and how a user toggles between
		// "which source modulates this cable's depth" and "by how much". Which of the two shows up
		// as the back edge depends on which the walk enters first, and the tree reaches each of them
		// first from somewhere (`patch_cables` chains straight to the strength page, every
		// `PatchedParam` reaches the picker first), so both do.
		//
		// Named by identity, not by `name`: the two share the string "Modulate depth".
		const auto name_of = [](const Node* n) -> std::string {
			if (n == &nodes::source_selection_range) {
				return "source_selection_range";
			}
			if (n == &nodes::patch_cable_strength_range) {
				return "patch_cable_strength_range";
			}
			return "UNEXPECTED:" + text_of(n->name);
		};
		// The set orders by pointer, which is link-order-dependent, so the names are sorted before
		// they are joined -- and compared as one string, so an unintended cycle prints the edge that
		// caused it rather than a bare `false`.
		std::vector<std::string> names;
		for (const auto& [from, to] : back_edges) {
			names.push_back(name_of(from) + " -> " + name_of(to));
		}
		std::sort(names.begin(), names.end());
		std::string named;
		for (const std::string& one : names) {
			named += one + "; ";
		}
		expect(named).to_equal(std::string{"patch_cable_strength_range -> source_selection_range; "
		                                   "source_selection_range -> patch_cable_strength_range; "});
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

describe tables_3_reset_items("scenes::menu batch-3 tables: the five reset_* items", $ {
	// Task 6 shipped `ContextMenuScene`, its `Descriptor` table and all five accept hooks, driven
	// and cross-checked against legacy directly by `DescriptorId`
	// (`context_menu_scene_spec.cpp`'s `cm_actions`). What that spec does NOT drive is the settings
	// tree itself: it opens `ContextMenuScene` straight off a `DescriptorId`, never through
	// `nodes::reset_flash` .. `nodes::reset_all`. This is the missing half -- the mapping from menu
	// node to descriptor -- and it is the one thing a swapped pair of `ResetItem` ids would break
	// silently if it were untested.
	namespace context_menu = deluge::scenes::context_menu;

	it("opens the reset context menu on the matching descriptor, rather than resetting on the spot", _ {
		// Real descent from `reset_settings` (the "Reset Settings" submenu, `settings.h`'s
		// `kResetSettingsChildren`) -- cursor down to row `i`, then the select-encoder press that
		// `enter_child` -> `enter_leaf` -> `CustomItem::on_focus` runs on, the same path
		// `rename_scene_spec.cpp`'s "opens on the drum target" test drives `EditNameItem` through.
		deluge::notify::dirty().drain();
		// A stray notice left in the global queue by an earlier describe block in this same process
		// would otherwise push a `ConsoleScene` overlay on this test's very first tick, one deeper
		// than the plain menu-then-context-menu stack this counts -- `tables_3_pixels`' own
		// `before_each` drains both queues for exactly this reason.
		{
			deluge::notify::Notice stray;
			while (deluge::notify::notices().pop(stray)) {}
		}
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);

		const context_menu::DescriptorId kOrder[] = {
		    context_menu::DescriptorId::ResetFlash,
		    context_menu::DescriptorId::ResetCommunityFeatures,
		    context_menu::DescriptorId::ResetMidiFollow,
		    context_menu::DescriptorId::ResetMidiDevices,
		    context_menu::DescriptorId::ResetAll,
		};
		for (size_t i = 0; i < std::size(kOrder); i++) {
			Rig r;
			r.open(nodes::reset_settings, settings.ctx);
			r.shell.tick(0);
			for (size_t t = 0; t < i; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.shell.tick(110);

			// `CustomItem::on_focus` fired from `enter_leaf`, and this tick's `after_render` turned
			// the resulting `MenuState::pending_scene` into a `Push` -- the context menu is on top.
			expect(&r.shell.stack().input_top() == &context_menu::context_menu_scene()).to_be_true();
			expect(r.shell.stack().depth()).to_equal(size_t(2));
			expect(static_cast<int>(context_menu::context_menu_scene().descriptor_id()))
			    .to_equal(static_cast<int>(kOrder[i]));
		}
	});

	it("resets exactly what legacy resets, driven through the real node, the real push and the real "
	   "accept button -- not just through a DescriptorId",
	   _ {
		   NoSdRoot no_card;
		   deluge::notify::dirty().drain();
		   {
			   deluge::notify::Notice stray;
			   while (deluge::notify::notices().pop(stray)) {}
		   }
		   const Root& settings = roots().back();
		   install(settings.ctx, settings.song);

		   // A sentinel moved off `runtimeFeatureSettings`'s own reset value, so "the reset ran" and
		   // "the reset did not run" are always distinguishable -- `context_menu_scene_spec.cpp`'s
		   // own idiom, driven here through the menu node instead of a bare `DescriptorId`.
		   runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, 1);

		   Rig r;
		   r.open(nodes::reset_settings, settings.ctx);
		   r.shell.tick(0);
		   // Row 1 is `reset_community_features` (`kResetSettingsChildren`'s own order).
		   r.turn(1, 10);
		   r.shell.tick(15);
		   r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		   r.shell.tick(110);
		   expect(&r.shell.stack().input_top() == &context_menu::context_menu_scene()).to_be_true();
		   expect(static_cast<int>(context_menu::context_menu_scene().descriptor_id()))
		       .to_equal(static_cast<int>(context_menu::DescriptorId::ResetCommunityFeatures));

		   // The one option ("OK") is already the cursor's position; the select-encoder press is
		   // `ContextMenu::buttonAction`'s `probablyAcceptCurrentOption` arm.
		   r.button(kSelectEncButtonX, kSelectEncButtonY, false, 120);
		   r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
		   r.shell.tick(140);
		   const uint32_t ported = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);

		   // `acceptCurrentOption` returns false for every shipped reset -> the menu removes itself,
		   // and `go_up_on_resume` (set alongside the push, `EditNameItem`'s own mechanism) steps the
		   // menu back out of the item it does not linger on -- back to the row it was entered from.
		   expect(r.shell.stack().depth()).to_equal(size_t(1));
		   expect(menu_scene().focused() == &nodes::reset_settings).to_be_true();

		   runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, 1);
		   gui::context_menu::resetSettings.setAction(gui::context_menu::ResetSettingsAction::CommunityFeatures);
		   gui::context_menu::resetSettings.currentOption = 0;
		   (void)gui::context_menu::resetSettings.acceptCurrentOption();
		   const uint32_t legacy = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);

		   expect(ported).to_equal(legacy);
	   });
});

describe tables_3_midi_commands("scenes::menu batch-3 tables: the twelve midi_command_* items", $ {
	// `midi::Command` (`gui/menu_item/midi/command.{h,cpp}`) is a MIDI *learn* item: focus it, send a
	// message, and the message is bound to one `GlobalMIDICommand`. The port is one
	// `nodes::MidiCommandItem` parameterised by that command, and `MenuScene::learn_note_on` /
	// `learn_cc` / `learn_program_change` / `unlearn` are the entry points
	// `SoundEditor::noteOnReceivedForMidiLearn` / `midiCCReceived` / `pcReceivedForMidiLearn` /
	// `unlearnAction()` become (`gui/ui/sound_editor.cpp:478`, `:1550-1570`).
	//
	// **What these examples do NOT cover.** Nothing routes MIDI into the scene stack yet -- that
	// wiring is a later plan's -- so every example below calls the entry points directly, exactly as
	// the wiring will. The gate legacy puts in front of them (`currentUIMode == UI_MODE_MIDI_LEARN`
	// and `!Buttons::isShiftButtonPressed()`, `sound_editor.cpp:1550`, `:1562`) is not ported and is
	// not tested here: it reads UI-mode state a scene may not touch.

	it("binds each node to the same GlobalMIDICommand as the legacy item in the same position", _ {
		// Positional, off `tree()`: the port's `kMidiCommandsChildren` walked in lockstep with
		// `midiCommandsMenu`'s own children (`gui/ui/menus.cpp:1430-1436`). A swapped pair of
		// `MidiCommandItem` arguments fails right here, before any learn runs.
		expect(command_pairs().size()).to_equal(size_t(12));
		for (const CommandPair& p : command_pairs()) {
			expect(p.port != nullptr).to_be_true();   // the node really is a MidiCommandItem now
			expect(p.legacy != nullptr).to_be_true(); // and legacy's really is a midi::Command
			if (p.port == nullptr || p.legacy == nullptr) {
				continue;
			}
			expect(static_cast<int>(p.port->command())).to_equal(static_cast<int>(p.legacy->commandNumber));
		}
	});

	it("learns a CC onto exactly the slot the legacy item learns it onto, driven through the real "
	   "descent",
	   _ {
		   // The whole `globalMIDICommands` array is compared, not just the slot under test: an item
		   // that learned onto the wrong command is the failure mode a per-slot check cannot see.
		   const Root& settings = roots().back();
		   install(settings.ctx, settings.song);
		   const CommandSlots saved = CommandSlots::capture();

		   const std::vector<CommandPair>& pairs = command_pairs();
		   for (size_t i = 0; i < pairs.size(); i++) {
			   const int32_t cc = static_cast<int32_t>(40 + i);

			   CommandSlots::clear_all();
			   Rig r;
			   focus_command_row(r, settings.ctx, i);
			   expect(menu_scene().focused() == pairs[i].node).to_be_true();
			   expect(menu_scene().learn_cc(fake_cable(), 3, cc, 127)).to_be_true();
			   const std::string got = CommandSlots::capture().describe();

			   CommandSlots::clear_all();
			   {
				   LegacyLearnScope legacy_scope{pairs[i].legacy};
				   pairs[i].legacy->learnCC(fake_cable(), 3, cc, 127);
				   expect(legacy_scope.spy.popups.empty()).to_be_true(); // the current-item branch
			   }
			   const std::string want = CommandSlots::capture().describe();

			   expect(got).to_equal(want);
			   expect(got != std::string{"(all clear)"}).to_be_true(); // a no-op would match vacuously
		   }
		   saved.restore();
	   });

	it("learns a note-on and a program change the way the legacy item does, including FILL's refusal",
	   _ {
		   const Root& settings = roots().back();
		   install(settings.ctx, settings.song);
		   const CommandSlots saved = CommandSlots::capture();

		   const std::vector<CommandPair>& pairs = command_pairs();
		   for (size_t i = 0; i < pairs.size(); i++) {
			   // Note-on: `Command::learnNoteOn` (`command.cpp:113-124`) writes the raw channel.
			   CommandSlots::clear_all();
			   Rig note_rig;
			   focus_command_row(note_rig, settings.ctx, i);
			   expect(menu_scene().learn_note_on(fake_cable(), 5, static_cast<int32_t>(60 + i))).to_be_true();
			   const std::string note_got = CommandSlots::capture().describe();
			   CommandSlots::clear_all();
			   {
				   LegacyLearnScope legacy_scope{pairs[i].legacy};
				   expect(pairs[i].legacy->learnNoteOn(fake_cable(), 5, static_cast<int32_t>(60 + i))).to_be_true();
			   }
			   expect(note_got).to_equal(CommandSlots::capture().describe());

			   // Program change: `Command::learnProgramChange` (`command.cpp:105-111`) adds `IS_A_PC`
			   // to the channel -- except on FILL, which refuses with the `CANT_LEARN_PC` popup and
			   // writes nothing. `SoundEditor::pcReceivedForMidiLearn` still reports the message as
			   // used in both cases (`sound_editor.cpp:1550-1556`), and so does the port.
			   CommandSlots::clear_all();
			   Rig pc_rig;
			   focus_command_row(pc_rig, settings.ctx, i);
			   expect(menu_scene().learn_program_change(fake_cable(), 2, static_cast<int32_t>(7 + i))).to_be_true();
			   const std::string pc_got = CommandSlots::capture().describe();
			   // The port's own half of the refusal: `MidiCommandItem::learn_program_change` raises the
			   // same `CANT_LEARN_PC` string legacy pops, and only on FILL.
			   size_t refusals = 0;
			   for (deluge::notify::Notice n; deluge::notify::notices().pop(n);) {
				   refusals += (n.string_id == uint16_t(String::STRING_FOR_CANT_LEARN_PC)) ? 1 : 0;
			   }
			   CommandSlots::clear_all();
			   const bool is_fill = pairs[i].legacy->commandNumber == GlobalMIDICommand::FILL;
			   {
				   LegacyLearnScope legacy_scope{pairs[i].legacy};
				   pairs[i].legacy->learnProgramChange(fake_cable(), 2, static_cast<int32_t>(7 + i));
				   // FILL is the one that pops instead of binding, and the popup is the refusal.
				   expect(legacy_scope.spy.popups.size()).to_equal(size_t(is_fill ? 1 : 0));
				   if (is_fill) {
					   expect(legacy_scope.spy.popups[0])
					       .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_CANT_LEARN_PC)});
				   }
			   }
			   expect(pc_got).to_equal(CommandSlots::capture().describe());
			   expect(pc_got == std::string{"(all clear)"}).to_equal(is_fill);
			   expect(refusals).to_equal(size_t(is_fill ? 1 : 0));
		   }
		   saved.restore();
	   });

	it("stores an MPE zone the way legacy does, and renders it as a zone rather than a channel", _ {
		// Fix round 1, I3. This arm is live, not defensive: `PlaybackHandler::noteMessageReceived`
		// (`playback/playback_handler.cpp:2966-2968`) resolves the incoming channel through
		// `MIDIPort::channelToZone` (`io/midi/midi_device.cpp:236-238`) *before* dispatching to
		// `SoundEditor::noteOnReceivedForMidiLearn`, so a note-on learn from an MPE-configured input
		// port really does arrive as `MIDI_CHANNEL_MPE_LOWER_ZONE`/`UPPER_ZONE` and really does take
		// `MidiCommandItem::render`'s zone branch. (The CC path is unaffected -- `midiCCReceived`
		// is handed the raw channel.)
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();

		const int32_t kZones[] = {MIDI_CHANNEL_MPE_LOWER_ZONE, MIDI_CHANNEL_MPE_UPPER_ZONE};
		for (const int32_t zone : kZones) {
			CommandSlots::clear_all();
			Rig r;
			focus_command_row(r, settings.ctx, 0);
			expect(menu_scene().learn_note_on(fake_cable(), zone, 64)).to_be_true();
			const std::string got = CommandSlots::capture().describe();

			CommandSlots::clear_all();
			{
				LegacyLearnScope legacy_scope{command_pairs()[0].legacy};
				expect(command_pairs()[0].legacy->learnNoteOn(fake_cable(), zone, 64)).to_be_true();
			}
			expect(got).to_equal(CommandSlots::capture().describe());
			// A zone, not a channel: `channelOrZone >= 16` is `LearnedMIDI::isForMPEZone()`, and it
			// is below `IS_A_CC` (18), so the third row still reads "Note".
			expect(midiEngine.globalMIDICommands[1].channelOrZone).to_equal(uint8_t(zone));
		}

		// The zone *readout* is pinned by a golden in `tables_3_pixels` below -- an inequality
		// against the plain-channel frame would not have caught a deleted zone arm, since the
		// fallback prints "CHANNEL 17" and that differs from "CHANNEL 4" too. Confirmed by mutation.
		saved.restore();
	});

	it("ignores a zero-valued CC, still reporting the message as used", _ {
		// `Command::learnCC` (`command.cpp:126-130`): the release half of a momentary controller
		// binds nothing. Legacy's `SoundEditor::midiCCReceived` returns true for it regardless
		// (`sound_editor.cpp:1562-1570`), so the port does too.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();

		Rig r;
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().learn_cc(fake_cable(), 3, 42, 0)).to_be_true();
		expect(CommandSlots::capture().describe()).to_equal(std::string{"(all clear)"});
		saved.restore();
	});

	it("unlearns exactly the focused item's slot, and clears it on any select-encoder motion", _ {
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		const CommandSlots saved = CommandSlots::capture();

		// Row 0 is `midi_command_play`; row 2 is `midi_command_record` (`kMidiCommandsChildren`).
		CommandSlots::clear_all();
		{
			Rig r;
			focus_command_row(r, settings.ctx, 2);
			expect(menu_scene().learn_cc(fake_cable(), 3, 42, 127)).to_be_true();
		}
		const std::string learned = CommandSlots::capture().describe();
		{
			Rig r;
			focus_command_row(r, settings.ctx, 0);
			expect(menu_scene().learn_cc(fake_cable(), 4, 43, 127)).to_be_true();
			// `MenuScene::unlearn` -> `Command::unlearnAction` (`command.cpp:96-104`): only the
			// focused item forgets, so RECORD's binding survives PLAY's unlearn.
			menu_scene().unlearn();
		}
		expect(CommandSlots::capture().describe()).to_equal(learned);

		// `Command::selectEncoderAction` (`command.cpp:88-91`): a detent in either direction clears
		// the binding. Driven through the real encoder, not by calling the item.
		{
			Rig r;
			focus_command_row(r, settings.ctx, 2);
			r.turn(-1, 300);
			r.shell.tick(310);
		}
		expect(CommandSlots::capture().describe()).to_equal(std::string{"(all clear)"});
		saved.restore();
	});

	it("asks for the learn blink on these twelve and on nothing else, and lights the LEARN LED for "
	   "them",
	   _ {
		   // `MenuItem::shouldBlinkLearnLed` is true only on `midi::Command` (`command.h:34`; the
		   // base returns false, `menu_item.h:196`), and `SoundEditor::beginScreen` turns that into
		   // `blinkLed(IndicatorLED::LEARN)` or `setLedState(LEARN, false)` on every screen change
		   // (`sound_editor.cpp:911-916`). Board LED 7 is LEARN (`shell/profiles/deluge.cpp:39`).
		   constexpr uint8_t kLearnBoardLed = 7;
		   const Root& settings = roots().back();
		   install(settings.ctx, settings.song);

		   for (size_t i = 0; i < command_pairs().size(); i++) {
			   Rig r;
			   focus_command_row(r, settings.ctx, i);
			   expect(menu_scene().wants_learn_blink()).to_be_true();
			   // A blink is a phase, so sample across more than one half-period (110 ms) before
			   // concluding the LED never came on.
			   bool lit = false;
			   for (uint32_t t = 220; t <= 660; t += 55) {
				   r.shell.tick(t);
				   for (const auto& w : r.fake.led_writes) {
					   lit = lit || (w.first == kLearnBoardLed && w.second);
				   }
			   }
			   expect(lit).to_be_true();
		   }

		   // A plain value item under the same root: no blink, and the LED is never lit.
		   Rig plain;
		   plain.open(nodes::default_pad_brightness, settings.ctx);
		   plain.shell.tick(0);
		   expect(menu_scene().wants_learn_blink()).to_be_false();
		   for (uint32_t t = 55; t <= 660; t += 55) {
			   plain.shell.tick(t);
		   }
		   for (const auto& w : plain.fake.led_writes) {
			   expect(!(w.first == kLearnBoardLed && w.second)).to_be_true();
		   }

		   // And the submenu the twelve hang off: `MenuScene::learn_*` has no custom item to forward
		   // to, so every message is reported unused rather than silently swallowed.
		   const CommandSlots saved = CommandSlots::capture();
		   CommandSlots::clear_all();
		   Rig sub;
		   sub.open(nodes::midi_commands, settings.ctx);
		   sub.shell.tick(0);
		   expect(menu_scene().wants_learn_blink()).to_be_false();
		   expect(menu_scene().learn_cc(fake_cable(), 3, 42, 127)).to_be_false();
		   expect(menu_scene().learn_note_on(fake_cable(), 3, 42)).to_be_false();
		   expect(menu_scene().learn_program_change(fake_cable(), 3, 42)).to_be_false();
		   menu_scene().unlearn(); // a no-op, not a crash
		   expect(CommandSlots::capture().describe()).to_equal(std::string{"(all clear)"});
		   saved.restore();
	   });

	it("has no param learn target: this is global-command learn, not param learn", _ {
		// `bindings::learn_target` is `PatchedParam`/`UnpatchedParam::getLearningThing()` -- "which
		// automatable parameter would a CC bind to". These twelve bind a whole message to a
		// transport action instead and carry no `ParamRef`, so the descriptor stays null. The two
		// learn paths share a name and nothing else.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().learn_target().isNull()).to_be_true();
	});
});

describe tables_3_dynamic("scenes::menu batch-3 tables: the CV/gate selectors and the MIDI device list", $ {
	// The last three settings items legacy implements as bespoke `MenuItem` subclasses rather than
	// as data: `cv::Selection` (`gui/menu_item/cv/selection.h:27-55`), `gate::Selection`
	// (`gui/menu_item/gate/selection.h:29-72`) and `midi::Devices`
	// (`gui/menu_item/midi/devices.h:24-39`, `devices.cpp`). The port is three `CustomItem`s
	// (`menu/nodes/dynamic.h`) plus the `MenuState::source_choice` / `midi_cable_choice`
	// write-back `settings.h` recorded as owed.
	//
	// **These are NOT `Node::dynamic_children` nodes**, which is what the plan expected them to be.
	// A submenu row is labelled from `Node::name`, one static `uint16_t` l10n id
	// (`MenuScene::render_vertical`), and `Devices` labels its rows with
	// `MIDICable::getDisplayName()` (`devices.cpp:169`) -- a runtime string with no id at all --
	// while the two selectors' option lists are static in legacy
	// (`cv/selection.h:47-54`, `gate/selection.h:56-71`) over the compile-time
	// `NUM_PHYSICAL_CV_CHANNELS` / `NUM_GATE_CHANNELS`. See `nodes/dynamic.h`'s own note.

	/// @brief Connect a chosen set of cables for the scope, and put the manager back afterwards.
	///
	/// Every `MIDIDeviceManager` global here is process-wide and shared with every other example in
	/// this binary, so the whole of what this touches -- three `connectionFlags` and the hosted
	/// vector -- is saved and restored.
	struct CableFixture {
		std::vector<MIDICableUSBHosted*> saved_hosted;
		uint8_t saved_din, saved_up1, saved_up2;
		std::vector<std::unique_ptr<MIDICableUSBHosted>> owned;

		CableFixture() {
			saved_hosted.assign(MIDIDeviceManager::hostedMIDIDevices.begin(),
			                    MIDIDeviceManager::hostedMIDIDevices.end());
			saved_din = MIDIDeviceManager::dinMIDIPorts.connectionFlags;
			saved_up1 = MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags;
			saved_up2 = MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags;
		}
		~CableFixture() {
			MIDIDeviceManager::hostedMIDIDevices.clear();
			for (MIDICableUSBHosted* c : saved_hosted) {
				MIDIDeviceManager::hostedMIDIDevices.push_back(c);
			}
			MIDIDeviceManager::dinMIDIPorts.connectionFlags = saved_din;
			MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags = saved_up1;
			MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = saved_up2;
		}
		CableFixture(const CableFixture&) = delete;
		CableFixture& operator=(const CableFixture&) = delete;

		/// @brief `flags` on the three fixed cables, and one hosted cable per name in @p hosted.
		void set(uint8_t din, uint8_t up1, uint8_t up2, std::initializer_list<const char*> hosted) {
			MIDIDeviceManager::dinMIDIPorts.connectionFlags = din;
			MIDIDeviceManager::upstreamUSBMIDICable1.connectionFlags = up1;
			MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = up2;
			MIDIDeviceManager::hostedMIDIDevices.clear();
			for (const char* n : hosted) {
				auto cable = std::make_unique<MIDICableUSBHosted>();
				cable->name = n;
				cable->connectionFlags = 1;
				MIDIDeviceManager::hostedMIDIDevices.push_back(cable.get());
				owned.push_back(std::move(cable));
			}
		}
	};

	/// @brief What `Devices::drawPixelsForOled` would draw, built from the legacy item itself.
	///
	/// The independent sentinel this spec compares against: it walks `lowestDeviceNum` upward
	/// through the *legacy* `getCable` (`devices.cpp:132-152`) and keeps what legacy's own draw loop
	/// keeps (`:163-176`), rather than restating the port's rule.
	auto legacy_visible = [] {
		std::vector<MIDICable*> out;
		for (int32_t idx = -3; idx < static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size()); idx++) {
			MIDICable* cable = deluge::gui::menu_item::midi::devicesMenu.getCable(idx);
			if (cable != nullptr && cable->connectionFlags != 0u) {
				out.push_back(cable);
			}
		}
		return out;
	};

	it("lists exactly the cables legacy lists, in legacy's order, with DIN first", _ {
		CableFixture cables;
		cables.set(/*din=*/1, /*up1=*/1, /*up2=*/1, {"ALPHA", "BETA"});

		MIDICable* got[deluge::scenes::menu::nodes::kMaxDeviceRows];
		const size_t n = nodes::connected_cables(got);
		const std::vector<MIDICable*> want = legacy_visible();

		expect(n).to_equal(want.size());
		expect(n).to_equal(size_t(5)); // DIN, upstream 1, upstream 2, ALPHA, BETA
		for (size_t i = 0; i < n && i < want.size(); i++) {
			expect(got[i] == want[i]).to_be_true();
		}
		// DIN really is first, and it is first because `lowestDeviceNum` is -3 (`devices.cpp:36`).
		expect(n > 0 && got[0] == &MIDIDeviceManager::dinMIDIPorts).to_be_true();
		expect(std::string{got[0]->getDisplayName()})
		    .to_equal(std::string{MIDIDeviceManager::dinMIDIPorts.getDisplayName()});
	});

	it("drops the disconnected ones, and yields DIN alone when nothing else is connected", _ {
		CableFixture cables;
		cables.set(/*din=*/1, /*up1=*/0, /*up2=*/0, {});

		MIDICable* got[deluge::scenes::menu::nodes::kMaxDeviceRows];
		size_t n = nodes::connected_cables(got);
		expect(n).to_equal(legacy_visible().size());
		expect(n).to_equal(size_t(1));
		expect(got[0] == &MIDIDeviceManager::dinMIDIPorts).to_be_true();

		// A disconnected *hosted* device is dropped too -- legacy's own "Disconnected devices aren't
		// drawn, so they don't take up a row" (`devices.cpp:70`). Mutation-proved rather than
		// reasoned: adding one must not move the count.
		cables.set(/*din=*/1, /*up1=*/0, /*up2=*/0, {});
		auto ghost = std::make_unique<MIDICableUSBHosted>();
		ghost->name = "GHOST";
		ghost->connectionFlags = 0;
		MIDIDeviceManager::hostedMIDIDevices.push_back(ghost.get());
		n = nodes::connected_cables(got);
		expect(n).to_equal(legacy_visible().size());
		expect(n).to_equal(size_t(1));
		MIDIDeviceManager::hostedMIDIDevices.clear();
	});

	it("seeds the cursor on the cable the context already holds -- and on DIN when it holds none", _ {
		// `Devices::beginSession` (`devices.cpp:38-57`): the previously-current cable if it is still
		// listed, otherwise `lowestDeviceNum` -- "Start on DIN. That's the only one that'll always
		// be there." (`:52`).
		CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);

		MIDICable* listed[deluge::scenes::menu::nodes::kMaxDeviceRows];
		const size_t n = nodes::connected_cables(listed);
		expect(n).to_equal(size_t(5));

		for (size_t seed = 0; seed < n; seed++) {
			MenuContext ctx = settings.ctx;
			ctx.midi_cable = listed[seed];
			Rig r;
			r.open(nodes::midi_devices, ctx);
			r.shell.tick(0);
			expect(size_t(menu_scene().state().list[0].cursor)).to_equal(seed);
		}

		MenuContext none = settings.ctx;
		none.midi_cable = nullptr;
		Rig r;
		r.open(nodes::midi_devices, none);
		r.shell.tick(0);
		expect(int(menu_scene().state().list[0].cursor)).to_equal(0);
	});

	it("remembers the cable the user moved onto, through the context, on the next visit", _ {
		// The write-back half: `Devices::selectEncoderAction` assigns
		// `soundEditor.currentMIDICable` on every move (`devices.cpp:102`), and the next
		// `beginSession` reads it back to reselect that device (`:41-49`). The port routes the same
		// fact through `MenuState::midi_cable_choice` -> `MenuScene::after_render` ->
		// `MenuContext::midi_cable` -> `MidiDevicesItem::on_focus`, so the round trip is what proves
		// it: descend, move, leave, come back, and the cursor is where it was left.
		CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		const Root& settings = roots().back();
		MenuContext ctx = settings.ctx;
		ctx.midi_cable = nullptr;
		install(ctx, settings.song);

		// Which row `midi_devices` is on: its position among the children `relevant` admits here,
		// rather than a hard-coded number that a hidden sibling would silently shift.
		int32_t row = 0;
		for (const Node* kid : nodes::midi_settings.children) {
			if (kid == &nodes::midi_devices) {
				break;
			}
			if (kid != nullptr && (kid->relevant == nullptr || kid->relevant(ctx))) {
				row++;
			}
		}

		Rig r;
		r.open(nodes::midi_settings, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		for (int32_t t = 0; t < row; t++) {
			r.turn(1, at);
			r.shell.tick(at + 5);
			at += 10;
		}
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		r.shell.tick(at + 5);
		at += 10;
		expect(menu_scene().focused() == &nodes::midi_devices).to_be_true();
		expect(int(menu_scene().state().list[1].cursor)).to_equal(0);

		// Two detents down the device list, then out and back in.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		at += 10;
		r.turn(2, at);
		r.shell.tick(at + 5);
		at += 10;
		expect(int(menu_scene().state().list[1].cursor)).to_equal(2);

		r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		r.shell.tick(at + 5); // a leaf's select press with no chain goes up
		at += 10;
		expect(menu_scene().focused() == &nodes::midi_settings).to_be_true();
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		at += 10;
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		r.shell.tick(at + 5);
		expect(menu_scene().focused() == &nodes::midi_devices).to_be_true();
		// `on_focus` found the cable the move published, not DIN.
		expect(int(menu_scene().state().list[1].cursor)).to_equal(2);
	});

	it("lands on DIN when nothing at all is connected, rather than leaving a stale cable", _ {
		// Fix round 1, minor 2. `Devices::beginSession` assigns unconditionally (`devices.cpp:55`),
		// and `getCable(lowestDeviceNum)` is the DIN cable whatever its `connectionFlags` say --
		// the guard at `:133-136` rejects out-of-range *indices*, not disconnection. So an empty
		// row list still resolves to DIN; it must not leave `MenuContext::midi_cable` pointing at
		// the previous visit's cable.
		//
		// Read before the first tick on purpose: `MenuScene::open` -> `retarget` -> `enter_leaf`
		// runs `on_focus`, and `after_render` (which the first tick would run) drains the field.
		CableFixture cables;
		cables.set(/*din=*/0, /*up1=*/0, /*up2=*/0, {});
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);

		MIDICable* none[deluge::scenes::menu::nodes::kMaxDeviceRows];
		expect(nodes::connected_cables(none)).to_equal(size_t(0));

		MenuContext ctx = settings.ctx;
		ctx.midi_cable = &MIDIDeviceManager::upstreamUSBMIDICable2; // the stale one
		Rig r;
		r.open(nodes::midi_devices, ctx);
		expect(menu_scene().state().midi_cable_choice == &MIDIDeviceManager::dinMIDIPorts).to_be_true();
	});

	it("does not move the source index just because the selector was opened", _ {
		// Fix round 1, minor 3. Legacy's `beginSession` moves the *item's* value and leaves
		// `soundEditor.currentSourceIndex` alone; only `selectButtonPress` writes it
		// (`cv/selection.h:32-34` vs `:42`, `gate/selection.h:34-36` vs `:47`). So `on_focus`
		// publishes no `source_choice`, and the sentinel is still -1 after the descent that ran it.
		//
		// Read before the first tick, for the same reason as the example above. (The other half of
		// the same fix -- that *scrolling* publishes nothing either -- has no observable effect
		// through the scene, because every page that reads `source_index` is reached only through
		// the press that republishes it; it is a code-shape fix, and this pins the half that shows.)
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		for (const Node* selector : {&nodes::cv_selection, &nodes::gate_selection}) {
			Rig r;
			r.open(*selector, settings.ctx);
			expect(int(menu_scene().state().source_choice)).to_equal(-1);
		}
	});

	it("offers cv::Selection's own two options and chains onto the output the cursor names", _ {
		// The option text is legacy's `getOptions()` (`cv/selection.h:47-54`), read off the live
		// item; the *mapping* is proved by a sentinel per output, because "CV OUTPUT 1" and
		// "CV OUTPUT 2" are otherwise indistinguishable in a golden.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		const auto options = cvSelectionMenu.getOptions(legacy::Selection::OptType::FULL);
		expect(options.size()).to_equal(size_t(NUM_PHYSICAL_CV_CHANNELS));

		const uint8_t saved0 = cvEngine.cvChannels[0].voltsPerOctave;
		const uint8_t saved1 = cvEngine.cvChannels[1].voltsPerOctave;
		cvEngine.cvChannels[0].voltsPerOctave = 11;
		cvEngine.cvChannels[1].voltsPerOctave = 77;

		for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
			Rig r;
			r.open(nodes::cv_selection, settings.ctx);
			r.shell.tick(0);
			for (int32_t t = 0; t < which; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.shell.tick(110);
			// `cv::Selection::selectButtonPress` returns `&cvSubmenu` unconditionally (`:41-45`).
			expect(menu_scene().focused() == &nodes::cv_submenu).to_be_true();

			// ... and it wrote `currentSourceIndex` on the way (`:42`). The proof is which output
			// `cv_volts` now reads: `enter_leaf` loads `bindings::read` into `MenuState::edit_int`.
			r.button(kSelectEncButtonX, kSelectEncButtonY, false, 120);
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
			r.shell.tick(140);
			expect(menu_scene().focused() == &nodes::cv_volts).to_be_true();
			expect(menu_scene().state().edit_int).to_equal(which == 0 ? 11 : 77);
		}

		cvEngine.cvChannels[0].voltsPerOctave = saved0;
		cvEngine.cvChannels[1].voltsPerOctave = saved1;
	});

	it("offers gate::Selection's five options and chains where legacy chains, output by output", _ {
		// `gate::Selection::selectButtonPress` (`:43-54`): the one-past-the-last option is the
		// minimum-off-time page, every other is that output's mode page. Driven through the real
		// scene, and cross-checked against the live legacy item on every value.
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		expect(gateSelectionMenu.getOptions(legacy::Selection::OptType::FULL).size()).to_equal(size_t(NUM_GATE_CHANNELS + 1));

		GateType saved[NUM_GATE_CHANNELS];
		for (int32_t g = 0; g < NUM_GATE_CHANNELS; g++) {
			saved[g] = cvEngine.gateChannels[g].mode;
			// One distinguishable value per output: V_TRIG on the even ones, S_TRIG on the odd, so
			// "the mode page opened on output N" is checkable rather than merely plausible.
			cvEngine.gateChannels[g].mode = (g % 2 == 0) ? GateType::V_TRIG : GateType::S_TRIG;
		}

		for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
			gateSelectionMenu.setValue(which);
			::MenuItem* legacy = gateSelectionMenu.selectButtonPress();

			Rig r;
			r.open(nodes::gate_selection, settings.ctx);
			r.shell.tick(0);
			for (int32_t t = 0; t < which; t++) {
				r.turn(1, static_cast<uint32_t>(10 + t * 10));
				r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.shell.tick(110);

			const Node* want = legacy == &gateOffTimeMenu ? &nodes::gate_off_time : &nodes::gate_mode;
			expect(legacy == &gateOffTimeMenu || legacy == &gateModeMenu).to_be_true();
			expect(menu_scene().focused() == want).to_be_true();

			if (which < NUM_GATE_CHANNELS) {
				// `enter_leaf` loaded `gate::Mode::readCurrentValue` for the output the selector
				// picked (`gate/mode.h:37` over `cvEngine.gateChannels[currentSourceIndex]`).
				expect(menu_scene().state().enum_index)
				    .to_equal(static_cast<int32_t>((which % 2 == 0) ? GateType::V_TRIG : GateType::S_TRIG));
			}
		}

		for (int32_t g = 0; g < NUM_GATE_CHANNELS; g++) {
			cvEngine.gateChannels[g].mode = saved[g];
		}
	});

	it("offers CLOCK or RUN SIGNAL only on the outputs that carry them, once a selection has been "
	   "made through the scene",
	   _ {
		   // `gateModeMenu.updateOptions(value)` on the way through `selectButtonPress`
		   // (`gate/selection.h:52`, `gate/mode.h:53-72`). The port's counterpart is
		   // `gate_mode_names` reading `MenuContext::source_index`, so this is the second, independent
		   // proof that the selector's cursor really reached the context -- a different mechanism from
		   // the `enum_index` check above, on the same write.
		   const Root& settings = roots().back();
		   install(settings.ctx, settings.song);

		   for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
			   Rig r;
			   r.open(nodes::gate_selection, settings.ctx);
			   r.shell.tick(0);
			   for (int32_t t = 0; t < which; t++) {
				   r.turn(1, static_cast<uint32_t>(10 + t * 10));
				   r.shell.tick(static_cast<uint32_t>(15 + t * 10));
			   }
			   r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			   r.shell.tick(110);
			   expect(menu_scene().focused() == &nodes::gate_mode).to_be_true();

			   gateModeMenu.updateOptions(which);
			   const size_t legacy_n = gateModeMenu.getOptions(legacy::Selection::OptType::FULL).size();

			   MenuContext ctx = settings.ctx;
			   ctx.source_index = static_cast<int8_t>(which);
			   std::string_view names[menu::kMaxDynamicNames];
			   deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			   const size_t n = menu::bindings::names(nodes::gate_mode, ctx, names, scratch);
			   expect(n).to_equal(legacy_n);
			   expect(n).to_equal(size_t(which == WHICH_GATE_OUTPUT_IS_CLOCK || which == WHICH_GATE_OUTPUT_IS_RUN ? 3
			                                                                                                     : 2));
		   }
	   });

	it("renders the device list and both output selectors, on both panel geometries", _ {
		// Both geometries, because a row constant that is right on one panel and wrong on the other
		// is exactly the class of mistake a single golden cannot see. Nothing here hand-computes a
		// rect: both pages lay out through `ui::oled::menu_rect(f)`, like every other menu screen.
		deluge::notify::dirty().drain();
		{
			deluge::notify::Notice stray;
			while (deluge::notify::notices().pop(stray)) {}
		}
		CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		const Root& settings = roots().back();
		MenuContext ctx = settings.ctx;
		ctx.midi_cable = nullptr;
		install(ctx, settings.song);

		{
			Rig r;
			r.open(nodes::midi_devices, ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_midi_devices_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::midi_devices, ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_midi_devices_64.pbm", r.fake.pbm(0));
		}
		{
			Rig r;
			r.open(nodes::gate_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_gate_selection_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::gate_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_gate_selection_64.pbm", r.fake.pbm(0));
		}
		{
			Rig r;
			r.open(nodes::cv_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_cv_selection_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::cv_selection, ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_cv_selection_64.pbm", r.fake.pbm(0));
		}
	});

	it("is not a placeholder any more, and reports nothing under a strict validation", _ {
		const Root& settings = roots().back();
		install(settings.ctx, settings.song);
		expect(nodes::cv_selection.custom == &nodes::kCvSelectionItem).to_be_true();
		expect(nodes::gate_selection.custom == &nodes::kGateSelectionItem).to_be_true();
		expect(nodes::midi_devices.custom == &nodes::kMidiDevicesItem).to_be_true();
		expect(nodes::kCvSelectionItem.is_placeholder()).to_be_false();
		expect(nodes::kGateSelectionItem.is_placeholder()).to_be_false();
		expect(nodes::kMidiDevicesItem.is_placeholder()).to_be_false();
		// No `dynamic_children` anywhere in the tree -- the finding this task recorded, and the
		// reason `deluge_menu_dump`'s fail-closed arm still has no case to be designed against.
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		size_t dynamic = 0;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->dynamic_children != nullptr) {
				dynamic++;
			}
		}
		expect(dynamic).to_equal(size_t(0));

		const Node* subtree[] = {&nodes::cv_selection, &nodes::gate_selection, &nodes::midi_devices};
		const MenuContext contexts[] = {settings.ctx};
		const auto issues = menu::validate_tree(subtree, contexts, {}, {},
		                                        menu::ValidateOptions{.skip_shortcut_sweep = true});
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

	// `midi::Command::drawPixelsForOled` (`gui/menu_item/midi/command.cpp:33-87`) has exactly two
	// shapes, and both are recorded here: the one-line "COMMAND UNASSIGNED", and the three-line
	// device / channel / message-kind readout, stepping by the row pitch. The number columns are
	// legacy's `kTextSpacingX * 8` (channel) and `* 5` (note).
	//
	// The rows land on legacy's literal *canvas* rows 20 / 29 / 38 on both panels, which the port
	// reaches through two corrections rather than one literal (`nodes/midi_command.cpp`'s
	// `command_row0_y`, and `ui/core/metrics.h`): the frame origin already carries the panel's own
	// `visible_top`, which is 5 on the 48-row board and **0** on the 64-row one -- hence
	// `kMidiCommandRowY48` = 15 and `kMidiCommandRowY64` = 20, a pair, not a constant -- and
	// `kApple7Height7YAdjust` = -1 cancels the one-row drop `FontId::Apple7` (the height-9 Apple
	// face) has against legacy's own height-7 call. Fix round 1, I1: a single 48-row-derived
	// constant put the 64-row panel five rows high, and only a 64-row golden could see it, so the
	// learned readout is recorded on both.
	it("renders an unassigned MIDI command item", _ {
		const Root& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().focused() == &nodes::midi_command_play).to_be_true();
		r.shell.tick(300);
		expect_golden(self, "../ui/menu/menu_midi_command_unassigned_48.pbm", r.fake.pbm(0));
		saved.restore();
	});

	it("renders a MIDI command item with a learned CC", _ {
		const Root& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		// Channel 3 + `IS_A_CC`, CC 42: the readout is the cable's display name, "CHANNEL 4"
		// (1-based, with the CC band subtracted back off) and "CC 42".
		expect(menu_scene().learn_cc(fake_cable(), 3, 42, 127)).to_be_true();
		r.shell.tick(300);
		expect_golden(self, "../ui/menu/menu_midi_command_learned_48.pbm", r.fake.pbm(0));
		saved.restore();
	});

	it("renders a MIDI command item learned to an MPE zone", _ {
		// `command.cpp:54-58`: an MPE zone prints "MPE LOWER ZONE" on the channel row instead of
		// "CHANNEL n", and -- being below `IS_A_CC` -- still prints "Note" on the third. A golden,
		// not an inequality: deleting the zone arm falls through to "CHANNEL 17", which is different
		// from every other frame in this file and so passes any inequality check (fix round 1, I3).
		const Root& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r;
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().learn_note_on(fake_cable(), MIDI_CHANNEL_MPE_LOWER_ZONE, 64)).to_be_true();
		r.shell.tick(300);
		expect_golden(self, "../ui/menu/menu_midi_command_mpe_zone_48.pbm", r.fake.pbm(0));
		saved.restore();
	});

	it("renders a MIDI command item with a learned CC on a 64-row panel", _ {
		const Root& settings = roots().back();
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		Rig r{board_64(), profile_64()};
		focus_command_row(r, settings.ctx, 0);
		expect(menu_scene().learn_cc(fake_cable(), 3, 42, 127)).to_be_true();
		r.shell.tick(300);
		expect_golden(self, "../ui/menu/menu_midi_command_learned_64.pbm", r.fake.pbm(0));
		saved.restore();
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

// clang-format off
describe tables_4_items("scenes::menu batch-4 items: the last placeholders that were only placeholders", $ {
	/// The nine E2 task 11 retires. Named once, here, and every example below inserts into `covered`
	/// so the closing example can assert the set by identity -- a node dropped from a root, or an
	/// example that quietly stopped driving one, fails rather than shrinking a count with slack in it.
	static const std::set<const Node*> kBatch4 = {
	    &nodes::pad_colour_active, &nodes::pad_colour_stopped, &nodes::pad_colour_muted,
	    &nodes::pad_colour_soloed, &nodes::pad_colour_fill,    &nodes::pad_colour_once,
	    &nodes::flash_status,      &nodes::feature_dev_sysex,  &nodes::shortcuts_version,
	};
	static std::set<const Node*> covered;

	it("gives all six pad colours legacy's own nine options", _ {
		const Root& settings = installed_root("settings");
		for (const Node* n : {&nodes::pad_colour_active, &nodes::pad_colour_stopped, &nodes::pad_colour_muted,
		                      &nodes::pad_colour_soloed, &nodes::pad_colour_fill, &nodes::pad_colour_once}) {
			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t count = menu::bindings::names(*n, settings.ctx, got, scratch);
			expect(count).to_equal(size_t(legacy::kNumPadColours));
			// `Colour::getOptions` (`gui/menu_item/colour.h:50-58`) -- the same nine for every item,
			// so one legacy call is the oracle for all six.
			auto* oracle = dynamic_cast<legacy::Selection*>(legacy_item_for(nodes::pad_colour_active));
			expect(oracle != nullptr).to_be_true();
			const auto want = oracle->getOptions(legacy::Selection::OptType::FULL);
			expect(count).to_equal(want.size());
			for (size_t i = 0; i < count && i < want.size(); i++) {
				expect(std::string{got[i]}).to_equal(std::string{want[i]});
			}
			// `bindings::range` derives 0..8 from the option count (`bindings.cpp`, `Kind::Enum`),
			// which is what `Enumeration::selectEncoderAction` clamps against.
			const menu::IntRange r = menu::bindings::range(*n, settings.ctx);
			expect(r.min).to_equal(int32_t(0));
			expect(r.max).to_equal(int32_t(legacy::kNumPadColours - 1));
		}
	});

	it("maps each pad colour node onto its own legacy item, and onto no other", _ {
		// **The mutation proof, run rather than reasoned about.** All six items look alike -- same
		// nine options, same kind, adjacent template arguments -- so "each one writes something" is
		// worth nothing. This drives one node at a time from a common base and records *which*
		// legacy item moved; the observed permutation must be the identity, and the example then
		// shows that assertion is not vacuous by checking a single transposition of it fails.
		const Root& settings = installed_root("settings");
		const Node* nodes_in_order[] = {&nodes::pad_colour_active, &nodes::pad_colour_stopped,
		                                &nodes::pad_colour_muted,  &nodes::pad_colour_soloed,
		                                &nodes::pad_colour_fill,   &nodes::pad_colour_once};
		// `coloursSubmenu`'s order (`gui/ui/menus.cpp:1248-1253`) -- the oracle for the pairing.
		legacy::Colour* items_in_order[6];
		for (size_t i = 0; i < 6; i++) {
			items_in_order[i] = dynamic_cast<legacy::Colour*>(legacy_item_for(*nodes_in_order[i]));
			expect(items_in_order[i] != nullptr).to_be_true();
		}

		uint8_t saved[6];
		for (size_t i = 0; i < 6; i++) {
			saved[i] = items_in_order[i]->value;
		}

		std::vector<int32_t> observed;
		for (size_t i = 0; i < 6; i++) {
			// A common base that is not the value about to be written, so a hook that stores nothing
			// is visible (the `sentinel_for` reasoning, applied to a six-way mapping).
			for (auto* item : items_in_order) {
				item->value = legacy::Colour::RED;
			}
			const auto target = static_cast<int32_t>(legacy::Colour::PINK);
			menu::bindings::write(*nodes_in_order[i], settings.ctx, target);

			int32_t moved = -1;
			int32_t moved_count = 0;
			for (size_t j = 0; j < 6; j++) {
				if (items_in_order[j]->value != legacy::Colour::RED) {
					moved = static_cast<int32_t>(j);
					moved_count++;
				}
			}
			expect(moved_count).to_equal(int32_t(1)); // exactly one item moved, never two, never none
			expect(items_in_order[moved < 0 ? 0 : moved]->value).to_equal(uint8_t(target));
			observed.push_back(moved);
			covered.insert(nodes_in_order[i]);

			// And the reverse direction: the port reads back what legacy's own write left behind, on
			// the same byte. Driven from a sentinel the value under test is not already at.
			items_in_order[i]->value = legacy::Colour::RED;
			items_in_order[i]->setValue(static_cast<int32_t>(legacy::Colour::CYAN));
			items_in_order[i]->writeCurrentValue();
			expect(menu::bindings::read(*nodes_in_order[i], settings.ctx))
			    .to_equal(static_cast<int32_t>(legacy::Colour::CYAN));
		}

		const std::vector<int32_t> identity{0, 1, 2, 3, 4, 5};
		expect(observed).to_equal(identity);
		// The mutation: swapping any two of the six would produce this instead, and it must not
		// compare equal -- otherwise the line above would pass on a wrongly wired table.
		const std::vector<int32_t> transposed{1, 0, 2, 3, 4, 5};
		expect(observed == transposed).to_be_false();

		for (size_t i = 0; i < 6; i++) {
			items_in_order[i]->value = saved[i];
		}
	});

	it("edits PadLEDs::flashCursor, in legacy's own option order", _ {
		const Root& settings = installed_root("settings");
		const uint8_t saved = PadLEDs::flashCursor;

		// `flash::Status::getOptions` (`gui/menu_item/flash/status.h:33-42`) is FAST / DISABLED /
		// SLOW, which is `FLASH_CURSOR_FAST` 0, `_OFF` 1, `_SLOW` 2 (`hid/led/pad_leds.h:25-27`) --
		// a value order, not a reading order, so getting it wrong would silently mean "off" where
		// the user asked for "slow". Each option is driven from a sentinel that is not itself.
		for (int32_t v = 0; v <= 2; v++) {
			PadLEDs::flashCursor = static_cast<uint8_t>(v == 0 ? 2 : 0);
			menu::bindings::write(nodes::flash_status, settings.ctx, v);
			expect(int32_t(PadLEDs::flashCursor)).to_equal(v);

			PadLEDs::flashCursor = static_cast<uint8_t>(v == 0 ? 2 : 0);
			legacy_write(legacy_item_for(nodes::flash_status), v);
			expect(menu::bindings::read(nodes::flash_status, settings.ctx)).to_equal(v);
		}
		covered.insert(&nodes::flash_status);
		PadLEDs::flashCursor = saved;
	});

	it("offers dev sysex as OFF plus the eight-hex-digit key a write installs", _ {
		const Root& settings = installed_root("settings");
		const auto saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::DevSysexAllowed, 0);

		std::string_view got[menu::kMaxDynamicNames];
		deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
		const size_t count = menu::bindings::names(nodes::feature_dev_sysex, settings.ctx, got, scratch);
		expect(count).to_equal(size_t(2));
		// Option 0 is the shared OFF string; the legacy item's own list is the oracle for it, and the
		// only part of that list that is reproducible (`devSysexSetting.cpp:52-59` renders a random
		// key into option 1).
		auto* dev_sysex = dynamic_cast<legacy::Selection*>(legacy_item_for(nodes::feature_dev_sysex));
		expect(dev_sysex != nullptr).to_be_true();
		const auto want = dev_sysex->getOptions(legacy::Selection::OptType::FULL);
		expect(want.size()).to_equal(size_t(2));
		expect(std::string{got[0]}).to_equal(std::string{want[0]});

		const std::string on{got[1]};
		expect(on.size()).to_equal(size_t(13));
		expect(on.substr(0, 5)).to_equal(std::string{"on - "});
		expect(on.find('*') == std::string::npos).to_be_true(); // the template's asterisks are gone
		// UPPERCASE. `intToHex` goes through `halfByteToHexChar` (`util/c_string.cpp:7-14`), which
		// returns `55 + n` for a digit at or above ten -- `'A'..'F'`. The case is load-bearing:
		// `hexCharToHalfByte` (`:16-23`) tests `>= 65` and subtracts 55, so a lowercase `'a'` parses
		// back as 42, and a key the user cannot type back in is not a key. The example below drives
		// both formatters over one shared value rather than leaving this as a character-set assertion.
		for (size_t i = 5; i < on.size(); i++) {
			expect(std::string{"0123456789ABCDEF"}.find(on[i]) != std::string::npos).to_be_true();
		}

		// **Quiet-tick coverage.** The key is drawn once and cached, not redrawn per call: a
		// `Kind::Enum` node's names hook runs every render, and a fresh draw each time would make the
		// digits on screen flicker and, worse, disagree with the key the write installs. Ten
		// resolutions with no input between them must all read the same.
		for (int32_t i = 0; i < 10; i++) {
			std::string_view again[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch again_scratch[menu::kMaxDynamicNames];
			menu::bindings::names(nodes::feature_dev_sysex, settings.ctx, again, again_scratch);
			expect(std::string{again[1]}).to_equal(on);
		}

		// ON stores that very key -- read the stored word back and format it the way the option did.
		expect(menu::bindings::read(nodes::feature_dev_sysex, settings.ctx)).to_equal(int32_t(0));
		menu::bindings::write(nodes::feature_dev_sysex, settings.ctx, 1);
		const auto stored = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed);
		expect(stored != 0).to_be_true();
		char hex[9] = {};
		for (int32_t i = 7; i >= 0; i--) {
			hex[i] = "0123456789ABCDEF"[(stored >> (4 * (7 - i))) & 15];
		}
		expect(on.substr(5)).to_equal(std::string{hex});
		expect(menu::bindings::read(nodes::feature_dev_sysex, settings.ctx)).to_equal(int32_t(1));

		// OFF stores zero, and the legacy item agrees on both readings of the same stored word.
		menu::bindings::write(nodes::feature_dev_sysex, settings.ctx, 0);
		expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed)).to_equal(uint32_t(0));
		expect(legacy_value(dev_sysex)).to_equal(std::optional<int32_t>{0});
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::DevSysexAllowed, 1);
		expect(menu::bindings::read(nodes::feature_dev_sysex, settings.ctx)).to_equal(int32_t(1));
		expect(legacy_value(dev_sysex)).to_equal(std::optional<int32_t>{1});

		covered.insert(&nodes::feature_dev_sysex);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::DevSysexAllowed, saved);
	});

	it("formats the dev sysex key exactly as legacy's own intToHex does, over the same key", _ {
		// Fix round 1, Important 1. The shape assertions above pin the port against *itself*; this
		// pins it against legacy, which is the only thing that catches a wrong digit table.
		//
		// The two sides normally hold different keys -- each rolls its own from `getNoise()` while
		// the setting is off -- which is why `names_are_random` excludes this node from the generic
		// option-list comparison. But both adopt a *stored* key when there is one
		// (`DevSysexSetting::readCurrentValue`, `runtime_feature/devSysexSetting.cpp:33-35`, and
		// `dev_sysex_on_value` in `nodes/settings.cpp`), so storing a key first makes the comparison
		// exact: same input, two independent formatters, byte-for-byte equal output or a bug.
		const Root& settings = installed_root("settings");
		const auto saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed);
		auto* dev_sysex = dynamic_cast<legacy::Selection*>(legacy_item_for(nodes::feature_dev_sysex));
		expect(dev_sysex != nullptr).to_be_true();

		// Keys chosen to exercise every branch of `halfByteToHexChar` (`util/c_string.cpp:7-14`):
		// digits below ten, digits at or above it, a leading zero nibble, and the all-`F` extreme.
		for (const uint32_t key : {uint32_t(0x1A2B3C4D), uint32_t(0xABCDEF01), uint32_t(0x0000000F),
		                           uint32_t(0x7FFFFFFF), uint32_t(0x12345678)}) {
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::DevSysexAllowed, key);

			// Each side is driven through its own read first, which is what adopts the stored key --
			// legacy in `readCurrentValue` (reached here through `legacy_value`, exactly as every
			// other comparison in this file reaches it), the port in its `get` hook (reached through
			// `bindings::read`). Neither is handed the other's string, and neither `getOptions` nor
			// `names` refreshes the key on its own: `getOptions` formats whatever `onValue` already
			// holds, so skipping the read compares a stale key and silently passes on nothing.
			expect(legacy_value(dev_sysex)).to_equal(std::optional<int32_t>{1});
			expect(menu::bindings::read(nodes::feature_dev_sysex, settings.ctx)).to_equal(int32_t(1));

			const auto want = dev_sysex->getOptions(legacy::Selection::OptType::FULL);
			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t count = menu::bindings::names(nodes::feature_dev_sysex, settings.ctx, got, scratch);

			expect(count).to_equal(want.size());
			expect(count).to_equal(size_t(2));
			expect(std::string{got[0]}).to_equal(std::string{want[0]});
			expect(std::string{got[1]}).to_equal(std::string{want[1]}); // the whole "on - XXXXXXXX"

			// And say out loud what that equality is worth, so a future reader does not have to
			// reconstruct it: the digits are upper case, and they are this key's.
			char hex[9] = {};
			uint32_t n = key;
			for (int32_t i = 7; i >= 0; i--) {
				hex[i] = "0123456789ABCDEF"[n & 15];
				n >>= 4;
			}
			expect(std::string{got[1]}).to_equal(std::string{"on - "} + hex);
		}

		covered.insert(&nodes::feature_dev_sysex);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::DevSysexAllowed, saved);
	});

	it("round-trips the shortcuts version through the port's own copy", _ {
		const Root& settings = installed_root("settings");
		const auto saved = menu::shortcuts_version();

		// Two options, legacy's own (`shortcuts/version.h:28-35`).
		std::string_view got[menu::kMaxDynamicNames];
		deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
		const size_t count = menu::bindings::names(nodes::shortcuts_version, settings.ctx, got, scratch);
		auto* version_item = dynamic_cast<legacy::Selection*>(legacy_item_for(nodes::shortcuts_version));
		expect(version_item != nullptr).to_be_true();
		const auto want = version_item->getOptions(legacy::Selection::OptType::FULL);
		expect(count).to_equal(want.size());
		for (size_t i = 0; i < count && i < want.size(); i++) {
			expect(std::string{got[i]}).to_equal(std::string{want[i]});
		}

		// The round trip, each direction driven from the other value. This is the node the generic
		// value comparisons exclude (`value_is_port_owned`) -- the layer it selects is cross-checked
		// against legacy's mutated arrays in `shortcuts_spec.cpp`, which is where the tables live.
		menu::set_shortcuts_version(menu::ShortcutsVersion::V3);
		menu::bindings::write(nodes::shortcuts_version, settings.ctx, 0);
		expect(menu::bindings::read(nodes::shortcuts_version, settings.ctx)).to_equal(int32_t(0));
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();

		menu::set_shortcuts_version(menu::ShortcutsVersion::V1);
		menu::bindings::write(nodes::shortcuts_version, settings.ctx, 1);
		expect(menu::bindings::read(nodes::shortcuts_version, settings.ctx)).to_equal(int32_t(1));
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();

		covered.insert(&nodes::shortcuts_version);
		menu::set_shortcuts_version(saved);
	});

	it("round-trips all six pad colours through writeSettings and readSettings", _ {
		// Fix round 1, Important 3. Nothing anywhere exercised `FlashStorage::readSettings` /
		// `writeSettings`, so the byte-166 bug this task fixed (`flash_storage.cpp`, the ONCE colour
		// read back into FILL) could be reintroduced in silence. It is exactly the kind of bug a
		// value comparison cannot see: both the port and legacy read `padColours` correctly, and the
		// loader between flash and that array was the broken part.
		//
		// The host BSP backs the settings region with RAM (`src/bsp/host/host_bsp.c`), so this is a
		// real program/erase/read cycle rather than a stub.
		uint8_t saved[FlashStorage::NUM_PAD_COLOUR_SLOTS];
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			saved[i] = FlashStorage::padColours[i];
		}

		// Six DISTINCT colours, none of them a slot's own default and none repeated -- so a loader
		// that crosses two slots, drops one, or leaves one at its default is visible. Avoids RED in
		// both FILL and ONCE, which would trip the deliberate both-red guard
		// (`flash_storage.cpp`, immediately below the byte-166 branch) and mask the result.
		const uint8_t written[FlashStorage::NUM_PAD_COLOUR_SLOTS] = {
		    legacy::Colour::CYAN,  legacy::Colour::PINK,  legacy::Colour::WHITE,
		    legacy::Colour::AMBER, legacy::Colour::GREEN, legacy::Colour::BLUE,
		};
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			FlashStorage::padColours[i] = written[i];
		}

		FlashStorage::writeSettings();
		// Scribble over every slot with a value none of them was written as, so a `readSettings`
		// that simply fails to touch a slot cannot pass by leaving the pre-write value in place.
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			FlashStorage::padColours[i] = legacy::Colour::MAGENTA;
		}
		FlashStorage::readSettings();

		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			expect(FlashStorage::padColours[i]).to_equal(written[i]);
		}
		// And name the two the bug was actually about, so a failure reads as itself rather than as
		// "index 5 differs": byte 165 is FILL and byte 166 is ONCE, and each must come back its own.
		expect(FlashStorage::padColours[FlashStorage::PAD_COLOUR_FILL]).to_equal(uint8_t(legacy::Colour::GREEN));
		expect(FlashStorage::padColours[FlashStorage::PAD_COLOUR_ONCE]).to_equal(uint8_t(legacy::Colour::BLUE));

		// The port reads the restored bytes, which is the point of the round trip: a colour the user
		// set survives a power cycle and the menu shows it again.
		const Root& settings = installed_root("settings");
		expect(menu::bindings::read(nodes::pad_colour_fill, settings.ctx))
		    .to_equal(static_cast<int32_t>(legacy::Colour::GREEN));
		expect(menu::bindings::read(nodes::pad_colour_once, settings.ctx))
		    .to_equal(static_cast<int32_t>(legacy::Colour::BLUE));

		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			FlashStorage::padColours[i] = saved[i];
		}
	});

	it("has no placeholder left among the nine, and drove every one of them", _ {
		// By identity, not by count: this is the whole point of `covered`.
		expect(covered).to_equal(kBatch4);
		for (const Node* n : kBatch4) {
			expect(n->kind == menu::Kind::Enum).to_be_true();
			expect(n->custom == nullptr).to_be_true();
			expect(n->bind.get != nullptr && n->bind.set != nullptr).to_be_true();
		}
	});
});
// clang-format on

/// Ten `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`.
int tables_3_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(tables_3_shape, tables_3_values, tables_3_validation, tables_3_writes, tables_3_chaining,
	                          tables_3_reset_items, tables_3_midi_commands, tables_3_dynamic, tables_3_pixels,
	                          tables_4_items)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
