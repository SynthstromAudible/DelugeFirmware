/// @file
/// @brief The batch-2 menu tables -- the whole sound-editor root -- cross-checked against the
///        legacy menu items they replace.
///
/// Task 6 declared six subtrees (filters, compressor, unison, oscillators, envelopes, LFOs) and
/// stopped below the root. Task 7 declares the root itself and everything else hanging off it:
/// actions, master, arpeggiator, randomizer, FX, sidechain, voice, bend, the sequence direction
/// and the output-MIDI page. The legacy objects are still linked into this binary (they are
/// `gui/ui/menus.cpp`'s globals), so the whole transcription is checked against the originals
/// rather than against a second hand-written list:
///
///   * the tree walks in lockstep with the legacy tree -- same children, same order, same count,
///     at every level from `soundEditorRootMenu` down;
///   * each node's name is the legacy item's own `getName()`;
///   * each node's `ParamRef` is the legacy item's own `getParamKind()` / `getParamIndex()`;
///   * `bindings::read(node, ctx)` equals the legacy item's `readCurrentValue()` + `getValue()` on
///     the same fixture sound;
///   * each `Kind::Enum`'s names are the legacy `Selection::getOptions(FULL)` strings;
///   * each node's `relevant` predicate agrees with the legacy `isRelevant()` override;
///
/// plus `validate_tree` over the root (placeholders allowed, see the validation group) and two
/// `MenuScene` renders of the root list.
///
/// @note The SPEC may include `gui/menu_item/` and `gui/ui/menus.h`; the library may not
///       (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule
///       `bindings_spec.cpp`, `menu_scene_spec.cpp` and `tables_1_spec.cpp` run under.

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
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

// The chaining group's legacy headers render through an unqualified `OLED::main`, which resolves
// inside `gui/ui/menus.cpp` only because that file has a file-scope
// `using namespace deluge::hid::display`. This spec has no such directive, so name the type in the
// namespace those headers are compiled in -- exactly the workaround `tables_1_spec.cpp` uses for
// `gui/menu_item/filter_route.h`.
namespace deluge::gui::menu_item {
using deluge::hid::display::OLED;
} // namespace deluge::gui::menu_item
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/note_mode_for_drums.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/voice/polyphony.h"

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

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

/// The one legacy global this spec needs by name to walk the tree: everything else in the *shape*
/// comparison is reached by walking its children. `Submenu` rather than `HorizontalMenu` --
/// `soundEditorRootMenu` is a vertical list.
extern legacy::Submenu soundEditorRootMenu;

/// @name The chaining group's legacy globals.
///
/// `selectButtonPress()` is a per-item override, so unlike everything else here these cannot be
/// reached by walking -- four of the six targets are named by no submenu literal at all
/// (`gui/ui/menus.cpp:511-521`), which is the whole reason `Node::chain_targets` exists.
/// @{
extern legacy::arpeggiator::PresetMode arpPresetModeMenu;
extern legacy::voice::PolyphonyType polyphonyMenu;
extern legacy::unison::CountToStereoSpread numUnisonMenu;
/// @}

/// @name E2 task 9's own legacy globals: the three items and the four pages they chain through.
///
/// `patchCablesMenu` (`gui/ui/menus.cpp:1735`), `vibratoMenu` (`:1725`) and
/// `sidechainVolumeShortcutMenu` (`:806`) are the three the port replaces outright; the four page
/// singletons live beside their own classes and are already `extern`-declared by the headers above.
/// @{
extern legacy::PatchCables patchCablesMenu;
extern legacy::patch_cable_strength::Fixed vibratoMenu;
extern legacy::sidechain::VolumeShortcut sidechainVolumeShortcutMenu;
/// @}

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;

/// @brief Read a `Submenu`'s child list.
///
/// `Submenu::items` is protected with no public accessor, and only `HorizontalMenu` exposes
/// `getItems()` -- but half of the root's children are plain vertical `Submenu`s, and the root
/// itself is one. Naming the members through a derived class is the standard (and standard-blessed,
/// [class.protected]) way to reach them: the pointer-to-member is formed inside a class derived
/// from `Submenu`, which is exactly the access the rule permits, and its type is still
/// `... Submenu::*` so it applies to the base objects `menus.cpp` actually defines.
///
/// The alternative -- re-listing every submenu's children in this file -- would make the spec a
/// second copy of the thing under test rather than a check of it.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(legacy::Submenu& menu) { return menu.*(&SubmenuPeek::items); }
};

/// @brief The real app picks a language during boot; this spec renders text without booting.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

/// @brief Point the `soundEditor` globals at whatever @p ctx is editing.
///
/// A legacy `readCurrentValue()` / `writeCurrentValue()` reaches `soundEditor.current*`
/// (`gui/ui/sound_editor.h:62-73`) while `bindings::read`/`write` reach the same objects through
/// the `MenuContext` `context_for` resolved, so both must point at one fixture. Batch 2 adds two
/// more of those globals -- `currentArpSettings` (`:69`), which the whole arpeggiator and
/// randomizer surface reads, and `currentPriority` (`:71`).
///
/// `currentSong` is re-pointed too: each `make_*_song()` sets it, so a spec juggling three
/// fixtures has to say which one is live before calling into the model's `getCurrent*()` helpers.
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
	// `SoundEditor::setup` caches this once per entry (`sound_editor.cpp:1811-1816`), and
	// `arpeggiator::IncludeInKitArp` reads the cached copy. Computed here the way legacy computes
	// it -- straight from `Sound::allowNoteTails` over the fixture's own model stack -- so the
	// comparison against `bindings::allows_note_tails` stays a real one.
	soundEditor.allowsNoteTails = true;
	if (ctx.sound != nullptr) {
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);
		soundEditor.allowsNoteTails = ctx.sound->allowNoteTails(stack->addSoundFlags(), true);
	}
	// Legacy's affect-entire branches all test this (`unpatched_param.cpp:52` and the twenty-odd
	// items that spell the same condition); the port's half is `MenuContext::affect_entire_held`.
	// Kept in step so a spec that sets one sets the other.
	currentUIMode = ctx.affect_entire_held ? UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR : 0;
}

/// @brief One fixture and the `MenuContext` resolved from it.
struct Rigged {
	spec::SongFixture song;
	MenuContext ctx{};
};

/// @brief The synth clip every shape/value example runs against.
Rigged& fixture() {
	static Rigged f = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		Rigged r;
		r.song = spec::make_synth_song();
		r.ctx = menu::context_for(*r.song.song, r.song.clip, 0, false, false, /*kit_global_fx=*/false);
		return r;
	}();
	install(f.ctx, f.song.song);
	return f;
}

/// @brief A kit with **two** `SoundDrum`s, the first selected.
///
/// Needed for two things the synth clip cannot show: the kit-only relevance predicates (review
/// round 1, M6 -- against one synth context ~25 of the 30 new predicates agreed vacuously), and
/// every affect-entire sweep, which by definition needs more than one drum to be observable
/// (review round 1, I5).
struct KitRig {
	spec::SongFixture song;
	SoundDrum* drum_a = nullptr;
	SoundDrum* drum_b = nullptr;
	MenuContext ctx{};
};

KitRig& kit_fixture() {
	static KitRig k = [] {
		KitRig r;
		r.song = spec::make_kit_song();
		r.drum_a = spec::add_drum<SoundDrum>(r.song);
		r.drum_b = spec::add_drum<SoundDrum>(r.song);
		static_cast<Kit*>(r.song.instrument)->selectedDrum = r.drum_a;
		r.ctx = menu::context_for(*r.song.song, r.song.clip, 0, false, false, /*kit_global_fx=*/false);
		return r;
	}();
	return k;
}

/// @brief Re-resolve the kit context with @p drum selected, and install it.
MenuContext select_drum(KitRig& k, Drum* drum, bool affect_entire_held = false) {
	static_cast<Kit*>(k.song.instrument)->selectedDrum = drum;
	MenuContext ctx = menu::context_for(*k.song.song, k.song.clip, 0, false, false, /*kit_global_fx=*/false);
	ctx.affect_entire_held = affect_entire_held;
	install(ctx, k.song.song);
	return ctx;
}

/// @brief Why there is no MIDI-clip context here.
///
/// Every `isRelevant` override in this tree begins `static_cast<Sound*>(modControllable)` and
/// dereferences it (`osc/audio_recorder.h:62`, `synth_mode.h:70-72`, ...): the sound-editor root is
/// only ever opened over a `Sound`, and legacy segfaults if asked otherwise -- which it never is,
/// because a MIDI clip opens `soundEditorRootMenuMIDIOrCV` instead (task 8's tree). So the two
/// contexts a *faithful* comparison can use are a synth clip and a kit row with a `SoundDrum`
/// selected, and the `midi_cv` variants stay structurally unreachable from this root. That is not a
/// gap in the check: it is the very property that lets legacy give a variant pair one name, and the
/// "one of each pair" example below asserts it directly.

std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief Legacy items whose `getName()` is not the l10n string the constructor was given.
///
/// Keyed on the **node**, not on its name id (review round 1, M7): `STRING_FOR_FEEDBACK` also names
/// batch 1's two FM-modulator feedback params, whose legacy items have perfectly static names, and
/// exempting by id would have skipped those two silently. A `Node` is a distinct object per menu
/// item, so this set says exactly which items build a name at render time -- something a
/// `Node::name` (one static string id) cannot express:
///
///   * batch 1's, reachable again from the root: `filter::FilterParam`'s morph name
///     (`filter/param.h:38`), `submenu::ActualSource` (`submenu/actual_source.h:26-38`),
///     `envelope::EnvelopeMenu`, `osc::source::Volume`, `ModulatorLevel`, `osc::AudioRecorder`
///     (all `FormattedTitle`);
///   * `mod_fx::Depth_Patched` / `Feedback` / `Offset`, whose names come from
///     `modfx::getParamName(type, ...)` and so change with the selected mod-FX type
///     (`mod_fx/depth_patched.h:29-31`, `feedback.h:26-28`, `offset.h:27-29`);
///   * `reverb::RoomSize` and `reverb::Width`, which read "TIME" / "DIFFUSION" on the two newer
///     reverb models (`reverb/room_size.h:29-38`, `width.h:28-37`).
bool has_dynamic_name(const Node& node) {
	static const std::set<const Node*> kDynamic = {
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
	};
	return kDynamic.contains(&node);
}

/// @brief One `(node, legacy item)` pair to compare, and how deep in the tree it sits.
struct Pair {
	const Node* node;
	::MenuItem* item;
	/// @brief Edges from the root: the root itself is 0, its children 1, their children 2.
	int32_t depth;

	/// @brief The `soundEditor.navigationDepth` this item is *displayed* at.
	///
	/// Legacy sets it to 0 while the root list is on screen (`sound_editor.cpp:1847`) and
	/// increments on the way into a submenu (`:328`), so the items **inside** a first-level
	/// submenu are drawn at depth 1 -- i.e. one less than their own distance from the root. Two
	/// legacy `isRelevant` overrides read it (`bend_range/per_finger.h:41` is the one this tree
	/// reaches), so a faithful comparison has to supply it (review round 1, I1).
	int32_t navigation_depth() const { return depth > 0 ? depth - 1 : 0; }
};

/// @brief Walk the node tree and the legacy tree together, pairing them level by level.
///
/// @return False as soon as the two disagree on a child count -- the caller asserts that first, so
///         a shape mismatch is one failure rather than N confusing per-item ones.
bool walk(const Node& node, ::MenuItem& item, std::vector<Pair>& out, std::string& where, int32_t depth = 0) {
	out.push_back(Pair{&node, &item, depth});
	if (node.kind != menu::Kind::Submenu) {
		return true;
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(&item);
	if (submenu == nullptr) {
		where = "node is a submenu but legacy item is not: " + std::string{item.getName()};
		return false;
	}
	const auto& kids = SubmenuPeek::items_of(*submenu);
	if (kids.size() != node.children.size()) {
		where = "child count differs under " + std::string{item.getName()};
		return false;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		if (node.children[i] == nullptr || kids[i] == nullptr) {
			where = "null child under " + std::string{item.getName()};
			return false;
		}
		if (!walk(*node.children[i], *kids[i], out, where, depth + 1)) {
			return false;
		}
	}
	return true;
}

/// @brief Every `(node, legacy item)` pair in the sound-editor root tree, or an empty list plus a
///        reason when the two trees do not have the same shape.
struct Walk {
	std::vector<Pair> pairs;
	std::string problem;
};

const Walk& tree() {
	static Walk w = [] {
		(void)fixture();
		Walk result;
		if (!walk(nodes::sound_root, soundEditorRootMenu, result.pairs, result.problem)) {
			result.pairs.clear();
		}
		return result;
	}();
	return w;
}

/// @brief The legacy item's current value, read the way legacy reads it, or `std::nullopt` for an
///        item that holds no value at all (the bespoke `Kind::Custom` ones and the submenus).
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

/// @brief Call a legacy item's own `writeCurrentValue()`.
///
/// `Value<T>::writeCurrentValue` is `protected` (`gui/menu_item/value.h:52`) and the only public
/// route to it is `selectEncoderAction`, which also calls `renderUIsForOled()` -- a UI path this
/// spec has no business entering. Naming the member through a derived class is the same
/// [class.protected]-blessed route `SubmenuPeek` uses above: the pointer-to-member is formed inside
/// a class derived from `Value<T>`, and its type is still `void (Value<T>::*)()`.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief Drive @p item's legacy write path with @p v.
/// @return False for an item that holds no value (a submenu, or a bespoke `Kind::Custom` one).
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

/// @brief Nodes whose **legacy** `writeCurrentValue()` this spec cannot drive, and why.
///
/// Every one reaches a UI global that only a booted app has. The point of listing them here rather
/// than quietly narrowing the loop is that the list is short, named and citable.
///
/// **It suppresses the legacy call only** (fix round 2, I1). Skipping the pair used to take the
/// *port's* hook out of the oracle too, and the read comparison does not stand in for it:
/// `bindings::read(arp_preset)` sees only `arp->preset`, which `set_arp_preset` assigns trivially,
/// so `updateSettingsFromCurrentPreset()`, `flagForceArpRestart` and the whole
/// `silence_for_arp_toggle` block (`nodes/sound_arp.cpp:80-149`) had no assertion anywhere in the
/// suite. The `drives the port hook of every node the legacy write comparison excludes` example
/// below drives all four port hooks alone and asserts what each persists; its `covered` tally is
/// what makes a fifth exclusion without coverage a failure.
constexpr const Node* kLegacyWriteNeedsUi[] = {
    &nodes::synth_mode,  // `view.setKnobIndicatorLevels()` (`synth_mode.h:57`)
    &nodes::arp_mode,    // `allNotesOff` over `soundEditor.getCurrentModelStack` (`mode.h:45-65`)
    &nodes::arp_preset,  // the same block (`preset_mode.h:50-70`)
    &nodes::mod_fx_type, // `display->displayError` on failure (`mod_fx/type.h:42,52`)
};

bool legacy_write_needs_ui(const Node& node) {
	for (const Node* n : kLegacyWriteNeedsUi) {
		if (n == &node) {
			return true;
		}
	}
	return false;
}

/// @brief Reach `Output::activeClip`, which is `protected` (`model/output.h:235`) and whose only
///        public setter also re-sends MIDI program changes. The [class.protected] route
///        `SubmenuPeek` uses, applied to `Output`; never instantiated.
struct OutputPeek : ::Output {
	static Clip*& active_clip_of(::Output& out) { return out.*(&OutputPeek::activeClip); }
};

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

/// @brief Three values spanning @p r: the two endpoints and the middle.
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
		(void)menu::assign_node_ids(nodes::kSoundRootRoots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
	}

	/// @brief One select-encoder press edge, at its own matrix position (`kMatrix`,
	///        `shell/profiles/deluge.cpp` -- the same (4, 3) `menu_scene_spec.cpp` names).
	///
	/// Added for E2 task 9's goldens: the patch-cable strength and source-selection pages are
	/// reached only through a select press, so a golden of either has to press one.
	void enter(uint32_t at_ms) { shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 4, 3, 255}, at_ms); }
};

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry (the shape `menu_scene_spec.cpp` established).
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

// ---------------------------------------------------------------------------------------------
// The `selectButtonPress()` chaining group's helpers.

/// @brief The node `item` was ported to, for the six menus a chain in this tree can name.
///
/// Null in, null out -- legacy's `return nullptr` is "go up", which the port spells the same way.
/// Anything else unmapped returns a distinct sentinel through `chain_name` so an unexpected legacy
/// answer fails loudly instead of silently comparing equal to "ascend".
const Node* chained_node_for(::MenuItem* item) {
	if (item == nullptr) {
		return nullptr;
	}
	if (item == &legacy::arpeggiator::arpOctaveModeToNoteModeMenu) {
		return &nodes::arp_octave_mode_to_note_mode;
	}
	if (item == &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums) {
		return &nodes::arp_octave_mode_to_note_mode_drums;
	}
	if (item == &legacy::arpeggiator::arpNoteModeFromOctaveModeMenu) {
		return &nodes::arp_note_mode_from_octave_mode;
	}
	if (item == &legacy::arpeggiator::arpNoteModeFromOctaveModeMenuForDrums) {
		return &nodes::arp_note_mode_from_octave_mode_drums;
	}
	if (item == &legacy::unison::stereoSpreadMenu) {
		return &nodes::unison_spread;
	}
	if (item == &legacy::voice::polyphonicVoiceCountMenu) {
		return &nodes::voice_count;
	}
	// E2 task 9's four patch-cable pages.
	if (item == &legacy::source_selection::regularMenu) {
		return &nodes::source_selection_regular;
	}
	if (item == &legacy::source_selection::rangeMenu) {
		return &nodes::source_selection_range;
	}
	if (item == &legacy::patch_cable_strength::regularMenu) {
		return &nodes::patch_cable_strength_regular;
	}
	if (item == &legacy::patch_cable_strength::rangeMenu) {
		return &nodes::patch_cable_strength_range;
	}
	return nullptr; // caught by `chain_name`'s "UNMAPPED" arm below, never mistaken for "ascend"
}

/// @brief A node pointer as a name the failure message can print. Compared instead of the raw
///        pointers so a mismatch says *which* page each side chose.
std::string chain_name(const Node* n) {
	if (n == nullptr) {
		return "<ascend>";
	}
	// The four arp pages share their base's `name`/`title`, so the identity has to come from the
	// pointer, not from the text.
	if (n == &nodes::arp_octave_mode_to_note_mode) {
		return "arp_octave_mode_to_note_mode";
	}
	if (n == &nodes::arp_octave_mode_to_note_mode_drums) {
		return "arp_octave_mode_to_note_mode_drums";
	}
	if (n == &nodes::arp_note_mode_from_octave_mode) {
		return "arp_note_mode_from_octave_mode";
	}
	if (n == &nodes::arp_note_mode_from_octave_mode_drums) {
		return "arp_note_mode_from_octave_mode_drums";
	}
	if (n == &nodes::unison_spread) {
		return "unison_spread";
	}
	if (n == &nodes::voice_count) {
		return "voice_count";
	}
	// The four patch-cable pages come in same-named pairs (`source_selection_regular` and
	// `patch_cable_strength_regular` both say "Modulate with"), so these too must come from the
	// pointer rather than from the text.
	if (n == &nodes::source_selection_regular) {
		return "source_selection_regular";
	}
	if (n == &nodes::source_selection_range) {
		return "source_selection_range";
	}
	if (n == &nodes::patch_cable_strength_regular) {
		return "patch_cable_strength_regular";
	}
	if (n == &nodes::patch_cable_strength_range) {
		return "patch_cable_strength_range";
	}
	return "UNMAPPED:" + text_of(n->name);
}

/// @brief Does @p node's `chain_targets` span name @p target?
///
/// The half of the chain contract `validate_tree` cannot check: `Flag::ChainTargetsMismatch` tests
/// only that `select_chain` and `chain_targets` are both set or neither is, because the hook is
/// opaque to a static walk. A hook returning a node absent from its own span produces a page with
/// no id -- unaddressable by `SceneId::arg`, unvalidated, and missing from the dump -- with nothing
/// to notice. Every answer a spec actually exercises is checked against the span here.
bool chain_targets_contain(const Node& node, const Node* target) {
	return std::find(node.chain_targets.begin(), node.chain_targets.end(), target) != node.chain_targets.end();
}

/// @brief Is @p target named by some node's static `children` span, anywhere in the whole tree?
///
/// The point of the chain-only pages is that the answer is *no* for four of them: nothing but a
/// `chain_targets` span reaches them, in the port or in legacy.
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
		// Chain targets are traversed (so a page reached only *below* a chained page is still
		// searched) but never counted as a `children` hit themselves.
		for (const Node* t : n->chain_targets) {
			stack.push_back(t);
		}
	}
	return false;
}

} // namespace

// clang-format off
describe tables_2_shape("scenes::menu batch-2 tables: shape", $ {
	it("declares the sound-editor root with the legacy child count and order", _ {
		(void)fixture();
		// `menus.cpp`'s `soundEditorRootMenu` literal (at `ce4a79de0`, `:1521-1551`; at HEAD,
		// `:1772-1802`) lists **27** children, not the 28 the task brief's prose says -- the
		// brief's own enumeration of the names is 27 long, so the count was a miscount and the
		// literal is the authority. Asserted against the live object, not a copy of the list.
		const auto& kids = SubmenuPeek::items_of(soundEditorRootMenu);
		expect(kids.size()).to_equal(size_t(27));
		expect(nodes::sound_root.children.size()).to_equal(size_t(27));
	});

	it("walks the whole root tree in lockstep with the legacy tree", _ {
		const Walk& w = tree();
		expect(w.problem).to_equal(std::string{});
		expect(w.pairs.empty()).to_be_false();
	});

	it("gives every node the legacy item's own name", _ {
		for (const Pair& p : tree().pairs) {
			if (has_dynamic_name(*p.node)) {
				continue; // see has_dynamic_name's own note
			}
			expect(text_of(p.node->name) == std::string{p.item->getName()}).to_be_true();
		}
	});

	it("gives every node the legacy item's own ParamRef, or none where legacy has none", _ {
		for (const Pair& p : tree().pairs) {
			const auto kind = p.item->getParamKind();
			if (kind == deluge::modulation::params::Kind::NONE) {
				continue;
			}
			// A `Submenu` reports the kind of its first param child, not one of its own
			// (`gui/menu_item/submenu.cpp`'s getParamKind); only leaves are compared. A
			// `Kind::Custom` node is an E2 placeholder with no binding at all -- and the two that
			// stand in for `patch_cable_strength::Fixed` items (`vibrato`, `sidechain_volume`)
			// report a *patch cable* descriptor, not a param index
			// (`patch_cable_strength.cpp`'s `getParamIndex`), so there is nothing to compare.
			if (p.node->kind == menu::Kind::Submenu || p.node->kind == menu::Kind::Custom) {
				continue;
			}
			expect(uint32_t(p.node->bind.param.index) == p.item->getParamIndex()).to_be_true();
			// The *index* is the item's own; the *kind* is not, for an unpatched item -- see
			// `tables_1_spec`'s long note on `UnpatchedParam::getParamKind` reporting whatever
			// collection `SoundEditor::getCurrentModelStack` lands on with no root UI open.
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
	});

	it("binds MIDI/CC learn to exactly the param legacy would have learned", _ {
		Rigged& f = fixture();
		size_t learnable = 0;
		for (const Pair& p : tree().pairs) {
			auto* learner = dynamic_cast<::MenuItemWithCCLearning*>(p.item);
			if (learner == nullptr) {
				continue;
			}
			// `patch_cable_strength::PatchCableStrength` is a `MenuItemWithCCLearning` whose
			// learn target is a patch cable, not a param (`patch_cable_strength.cpp`'s
			// `getDestinationDescriptor`); the two nodes that stand in for one
			// (`vibrato`, `sidechain_volume`) are E2 placeholders with no binding at all, so they
			// are outside this comparison until E2 implements them.
			if (p.node->kind == menu::Kind::Custom) {
				continue;
			}
			learnable++;
			expect(menu::bindings::learn_target(*p.node, f.ctx) == learner->getLearningThing()).to_be_true();
		}
		// Every `ParamRef` leaf in the root tree plus the four transposes; a drop to 0 would make
		// the loop vacuous.
		expect(learnable > size_t(60)).to_be_true();
	});

	it("carries the legacy occupied-slot counts for the wide batch-2 items", _ {
		(void)fixture();
		// `MenuItem::getOccupiedSlots()` is protected, so the overridden counts are asserted
		// against the values their sources state: `mod_fx::Type` draws the whole four-slot page
		// (`mod_fx/type.h:60-63`), and both rhythm items take two
		// (`arpeggiator/rhythm.h:51-52`, `arpeggiator/midi_cv/rhythm.h:56-57`).
		expect(int(nodes::mod_fx_type.slots)).to_equal(4);
		expect(int(nodes::arp_rhythm.slots)).to_equal(2);
		expect(int(nodes::arp_rhythm_midi.slots)).to_equal(2);
	});
});

describe tables_2_values("scenes::menu batch-2 tables: values", $ {
	it("reads what the legacy item reads, for every node that holds a value", _ {
		Rigged& f = fixture();
		for (const Pair& p : tree().pairs) {
			const std::optional<int32_t> want = legacy_value(p.item);
			if (!want.has_value()) {
				continue; // a bespoke Kind::Custom item or a submenu -- nothing to read
			}
			if (p.node->kind == menu::Kind::Custom) {
				continue; // an E2 placeholder: the node deliberately carries no binding
			}
			expect(menu::bindings::read(*p.node, f.ctx) == *want).to_be_true();
		}
	});

	it("names every enum option exactly as the legacy Selection does", _ {
		Rigged& f = fixture();
		for (const Pair& p : tree().pairs) {
			if (p.node->kind != menu::Kind::Enum) {
				continue;
			}
			auto* selection = dynamic_cast<legacy::Selection*>(p.item);
			expect(selection != nullptr).to_be_true();
			if (selection == nullptr) {
				continue;
			}
			const auto want = selection->getOptions(legacy::Selection::OptType::FULL);

			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t n = menu::bindings::names(*p.node, f.ctx, got, scratch);
			expect(n == want.size()).to_be_true();
			for (size_t i = 0; i < n && i < want.size(); i++) {
				expect(std::string{got[i]} == std::string{want[i]}).to_be_true();
			}
		}
	});

	it("agrees with the legacy isRelevant under a synth clip and a kit row", _ {
		// Three contexts, not one (review round 1, M6): against a synth clip alone every
		// kit/MIDI/gate-drum predicate is false on both sides, so ~25 of the 30 new predicates
		// agreed vacuously -- which is exactly how I1 got through. A kit row turns on
		// `is_kit`/`is_kit_row`/`kit_row_not_gate`/`per_finger_bend`/`randomizer_non_kit`, and a
		// (A MIDI clip cannot be one of them -- see the note on `midi_fixture`'s absence above.)
		struct Case {
			const char* name;
			MenuContext ctx;
			Song* song;
		};
		Rigged& synth = fixture();
		KitRig& kit = kit_fixture();
		const MenuContext kit_ctx = select_drum(kit, kit.drum_a);
		const Case cases[] = {
		    {"synth", synth.ctx, synth.song.song},
		    {"kit row", kit_ctx, kit.song.song},
		};

		size_t disagreements_true = 0; // how many predicates were ever true, so this cannot go vacuous
		for (const Case& c : cases) {
			install(c.ctx, c.song);
			for (const Pair& p : tree().pairs) {
				// `bend_range::PerFinger::isRelevant` reads `soundEditor.navigationDepth`; supply
				// the depth this item is actually displayed at (see `Pair::navigation_depth`).
				soundEditor.navigationDepth = p.navigation_depth();
				const bool want = p.item->isRelevant(c.ctx.mod, c.ctx.source_index);
				const bool got = p.node->relevant == nullptr || p.node->relevant(c.ctx);
				expect(got == want).to_be_true();
				if (got) {
					disagreements_true++;
				}
			}
		}
		soundEditor.navigationDepth = 0;
		expect(disagreements_true > size_t(150)).to_be_true();
		install(synth.ctx, synth.song.song);
	});

	it("shows the per-finger bend range wherever legacy shows it", _ {
		// Review round 1, I1. `perFingerBendRangeMenu` lives only inside `bendMenu`, i.e. at
		// `navigationDepth == 1`, where `per_finger.h:41`'s first arm short-circuits to true --
		// so legacy always draws it on the Bend Range page, including on a synth where
		// `editingKit()` is false. The node must agree, or the page loses one of its two items.
		Rigged& synth = fixture();
		expect(nodes::bend_range_per_finger.relevant == nullptr).to_be_true();
		expect(nodes::bend.relevant != nullptr && nodes::bend.relevant(synth.ctx)).to_be_true();
		// ... while `drumBendRangeMenu`, the same class as a *root child* at depth 0, is kit-only.
		expect(nodes::drum_bend_range.relevant != nullptr).to_be_true();
		expect(nodes::drum_bend_range.relevant(synth.ctx)).to_be_false();
		KitRig& kit = kit_fixture();
		const MenuContext kit_ctx = select_drum(kit, kit.drum_a);
		expect(nodes::drum_bend_range.relevant(kit_ctx)).to_be_true();
		install(synth.ctx, synth.song.song);
	});

	it("gives every node the legacy item's own title", _ {
		// Review round 1, M8: ~40 `with_title(...)` transcriptions and every "title == name"
		// decision rested on reading alone.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t compared = 0;
		for (const Pair& p : tree().pairs) {
			if (has_dynamic_name(*p.node)) {
				continue; // the same five classes build their *title* from the name, at render time
			}
			const std::string title = text_of(p.node->title);
			// A title carrying the `*` placeholder is a `FormattedTitle`: legacy substitutes the
			// oscillator / envelope / LFO index into it at render time (`formatted_title.h`, via
			// `asterixToInt`), which one static `Node::title` cannot do. All 49 such items are
			// batch 1's -- the gap task 6 recorded -- and the marker is in the l10n string itself,
			// so this exemption names the mechanism rather than a list.
			if (title.find('*') != std::string::npos) {
				continue;
			}
			expect(title == std::string{p.item->getTitle()}).to_be_true();
			compared++;
		}
		expect(compared > size_t(120)).to_be_true();
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const Pair& p : tree().pairs) {
			if (p.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(p.node->relevant) != std::string_view{"?"}).to_be_true();
		}
		const auto registry = menu::relevance::registry();
		for (size_t i = 0; i < registry.size(); i++) {
			expect(registry[i].fn != nullptr).to_be_true();
			expect(registry[i].name.empty()).to_be_false();
			for (size_t j = i + 1; j < registry.size(); j++) {
				expect(registry[i].fn == registry[j].fn).to_be_false();
				expect(registry[i].name == registry[j].name).to_be_false();
			}
		}
	});
});

describe tables_2_validation("scenes::menu batch-2 tables: validation", $ {
	it("validates the sound root clean apart from the nine legacy name collisions", _ {
		Rigged& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		const auto issues = menu::validate_tree(
		    nodes::kSoundRootRoots, contexts, nodes::kRangeDependentNodes, nodes::kHalfPrecisionNodes,
		    menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		size_t duplicates = 0;
		size_t others = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::DuplicateName) {
				duplicates++;
			}
			else {
				others++;
			}
		}
		// Nine sibling collisions, every one a legacy `FormattedTitle` -- a name legacy builds at
		// render time by substituting an index, which one `uint16_t Node::name` cannot express:
		//
		//   * the three pairs `tables_1_spec` blesses -- `sample0/1RecorderMenu`
		//     (`STRING_FOR_RECORD_TO_SAMPLE`, `osc/audio_recorder.h:35`), `source0/1VolumeMenu`
		//     (`STRING_FOR_OSC_LEVEL_MENU_TITLE`, `osc/source/volume.h:28`) and
		//     `modulator0/1Volume` (`STRING_FOR_FM_MOD_LEVEL_MENU_TITLE`, `modulator_level.h:27-28`);
		//   * six more that only appear once the root itself is declared: `env1Menu` .. `env4Menu`
		//     are four *siblings* of `soundEditorRootMenu`, all named
		//     `STRING_FOR_ENVELOPE_MENU_TITLE` (`envelope/envelope_menu.h:31`) -- four items
		//     colliding pairwise is six reports. Task 6 could not see them: it declared the
		//     envelopes as four separate roots, with no parent to be siblings under.
		//
		// Everything else legacy names twice under one parent is filtered by `validate.cpp`'s
		// `co_visible` -- the randomizer's eleven sound/MIDI pairs, the arpeggiator's rhythm,
		// sequence-length and note-mode twins, and `bendMenu` beside `drumBendRangeMenu` -- because
		// no context shows both. The rule still fires on a real collision, which is what these nine
		// are: E2's formatted-name field on `Node` is what removes them, and Task 12 records the
		// tolerance as a spec E §4.4 amendment.
		expect(duplicates).to_equal(size_t(9));
		expect(others).to_equal(size_t(0));
	});

	it("assigns an id to every node in the sound root tree", _ {
		Rigged& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		(void)menu::validate_tree(
		    nodes::kSoundRootRoots, contexts, {}, {},
		    menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		for (const Pair& p : tree().pairs) {
			expect(menu::node_id(*p.node) != uint16_t(0)).to_be_true();
		}
	});
});

describe tables_2_chaining("scenes::menu batch-2 tables: selectButtonPress chaining", $ {
	it("answers what the legacy item's own selectButtonPress answers, value by value", _ {
		// The comparison is against `MenuItem::selectButtonPress()` on the live legacy globals, not
		// against a hand-written expectation: `chained_node_for` maps the `MenuItem*` legacy returns
		// back to the node it was ported to, so a wrong condition, a wrong target, or a missing
		// `select_chain` all show up as a mismatch here rather than as a passing tautology.
		Rigged& f = fixture();

		struct Case {
			const Node* node;
			::MenuItem* item;
			int32_t value; ///< written to both sides before the press
		};
		const Case cases[] = {
		    // `PresetMode::selectButtonPress` (`preset_mode.h:109-118`): CUSTOM opens a page, every
		    // other preset ascends. Both ends of that are probed, not just the interesting one.
		    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::CUSTOM)},
		    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::UP)},
		    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::OFF)},
		    // `PolyphonyType::selectButtonPress` (`voice/polyphony.h:139-143`): POLY opens the voice
		    // count, everything else falls through to `Selection::selectButtonPress`, which for the
		    // open menu is "go up" (`selection.h:54-64`).
		    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::POLY)},
		    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::MONO)},
		    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::AUTO)},
		    // `CountToStereoSpread::selectButtonPress` (`unison/count.h:76-79`): unconditional, so
		    // two different counts must give the same answer.
		    {&nodes::unison_count, &numUnisonMenu, 1},
		    {&nodes::unison_count, &numUnisonMenu, 4},
		    // `OctaveModeToNoteMode` / its drum twin (`octave_mode.h:85-100`): also unconditional.
		    {&nodes::arp_octave_mode_to_note_mode, &legacy::arpeggiator::arpOctaveModeToNoteModeMenu, 0},
		    {&nodes::arp_octave_mode_to_note_mode_drums, &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums, 0},
		};

		size_t compared = 0;
		for (const Case& c : cases) {
			install(f.ctx, f.song.song);
			menu::bindings::write(*c.node, f.ctx, c.value);
			// `Selection::selectButtonPress`'s fall-through arm asks whether the item IS the open
			// menu (`selection.h:57`). In the port the chain hook only ever runs on the focused
			// leaf, so that is the state to put legacy in.
			soundEditor.navigationDepth = 0;
			soundEditor.menuItemNavigationRecord[0] = c.item;
			c.item->readCurrentValue();

			::MenuItem* legacy_answer = c.item->selectButtonPress();
			const Node* want = chained_node_for(legacy_answer);
			const Node* got = c.node->select_chain != nullptr ? c.node->select_chain(f.ctx, menu_scene().state())
			                                                  : nullptr;
			expect(chain_name(got)).to_equal(chain_name(want));
			// Every answer the hook actually gives must be in its own `chain_targets` span --
			// otherwise the page it names is unnumbered, unaddressable by `SceneId::arg` and absent
			// from the dump, silently. `validate_tree` can only check both-set-or-neither (the hook
			// is opaque to a static walk), so the containment half is checked on exercised answers.
			expect(got == nullptr || chain_targets_contain(*c.node, got)).to_be_true();
			compared++;
		}
		soundEditor.menuItemNavigationRecord[0] = nullptr;
		expect(compared).to_equal(std::size(cases));
	});

	it("opens the drum twin of the octave-mode page when a kit row is being edited", _ {
		// The other half of `PresetMode::selectButtonPress` (`preset_mode.h:112-115`):
		// `soundEditor.editingKitRow()` picks `arpOctaveModeToNoteModeMenuForDrums`. The synth
		// fixture above can never show it, so it needs the kit fixture.
		KitRig& k = kit_fixture();
		const MenuContext ctx = select_drum(k, k.drum_a);
		menu::bindings::write(nodes::arp_preset, ctx, int32_t(ArpPreset::CUSTOM));

		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = &arpPresetModeMenu;
		arpPresetModeMenu.readCurrentValue();
		const Node* want = chained_node_for(arpPresetModeMenu.selectButtonPress());
		soundEditor.menuItemNavigationRecord[0] = nullptr;

		expect(want == &nodes::arp_octave_mode_to_note_mode_drums).to_be_true();
		expect(nodes::arp_preset.select_chain(ctx, menu_scene().state())
		       == &nodes::arp_octave_mode_to_note_mode_drums)
		    .to_be_true();

		// And the drum twin chains on to the drum note mode, not the plain one.
		expect(nodes::arp_octave_mode_to_note_mode_drums.select_chain(ctx, menu_scene().state())
		       == &nodes::arp_note_mode_from_octave_mode_drums)
		    .to_be_true();
		expect(nodes::arp_octave_mode_to_note_mode.select_chain(ctx, menu_scene().state())
		       == &nodes::arp_note_mode_from_octave_mode)
		    .to_be_true();
		install(fixture().ctx, fixture().song.song);
	});

	it("declares the four chain-only arp pages exactly as their legacy base class reads and writes", _ {
		// `arpOctaveModeToNoteModeMenu` and its three relatives override nothing but
		// `selectButtonPress` (`octave_mode.h:85-100`, `note_mode.h:70-76`,
		// `note_mode_for_drums.h:78-84`), so each must be indistinguishable from the page already in
		// `arpPatternMenu` apart from the chain itself. Comparing against the sibling node -- not
		// against a copy of the field values -- is what makes this fail if one drifts.
		struct Twin {
			const Node* chained;
			const Node* plain;
		};
		const Twin twins[] = {
		    {&nodes::arp_octave_mode_to_note_mode, &nodes::arp_octave_mode},
		    {&nodes::arp_octave_mode_to_note_mode_drums, &nodes::arp_octave_mode},
		    {&nodes::arp_note_mode_from_octave_mode, &nodes::arp_note_mode},
		    {&nodes::arp_note_mode_from_octave_mode_drums, &nodes::arp_note_mode_drums},
		};
		for (const Twin& t : twins) {
			expect(t.chained->name).to_equal(t.plain->name);
			expect(t.chained->title).to_equal(t.plain->title);
			expect(static_cast<int>(t.chained->kind)).to_equal(static_cast<int>(t.plain->kind));
			expect(t.chained->relevant == t.plain->relevant).to_be_true();
			expect(t.chained->bind.get == t.plain->bind.get).to_be_true();
			expect(t.chained->bind.set == t.plain->bind.set).to_be_true();
			expect(t.chained->bind.names.data() == t.plain->bind.names.data()).to_be_true();
			// ... and is a page in its own right, not one of the plain ones under another name.
			expect(t.chained != t.plain).to_be_true();
		}
	});

	it("numbers every chain target, including the four pages no children span reaches", _ {
		// The reachability ruling: `assign_node_ids` follows `chain_targets` as well as `children`,
		// or `SceneId::arg` could not name a chained page and `MenuScene::on_enter` could not
		// restore one. Asserted over the WHOLE tree, since that is the walk the app performs.
		(void)menu::assign_node_ids(nodes::kAllRoots);
		const Node* chained[] = {
		    &nodes::arp_octave_mode_to_note_mode,       &nodes::arp_octave_mode_to_note_mode_drums,
		    &nodes::arp_note_mode_from_octave_mode,     &nodes::arp_note_mode_from_octave_mode_drums,
		    &nodes::unison_spread,                      &nodes::voice_count,
		};
		for (const Node* n : chained) {
			const uint16_t id = menu::node_id(*n);
			expect(id != uint16_t(0)).to_be_true();
			expect(menu::node_by_id(id) == n).to_be_true();
		}
		// The four arp pages are reachable ONLY through a chain -- no span in the tree names them.
		const Node* chain_only[] = {&nodes::arp_octave_mode_to_note_mode,
		                            &nodes::arp_octave_mode_to_note_mode_drums,
		                            &nodes::arp_note_mode_from_octave_mode,
		                            &nodes::arp_note_mode_from_octave_mode_drums};
		for (const Node* n : chain_only) {
			expect(reachable_by_children(n)).to_be_false();
		}
		// ... while `unison_spread` and `voice_count` are ordinary columns as well as chain targets.
		expect(reachable_by_children(&nodes::unison_spread)).to_be_true();
		expect(reachable_by_children(&nodes::voice_count)).to_be_true();
	});

	it("sets select_chain and chain_targets together, everywhere in the tree", _ {
		// `Flag::ChainTargetsMismatch`: a hook with no targets is a page nothing can number; targets
		// with no hook is a page nothing can open. Checked over `kAllRoots` through the validator
		// itself, so the rule is exercised and not merely present.
		Rigged& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		const auto issues =
		    menu::validate_tree(nodes::kAllRoots, contexts, {}, {},
		                        menu::ValidateOptions{.allow_placeholder_custom = true, .skip_shortcut_sweep = true});
		size_t mismatches = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::FlagMismatch
			    && i.detail == static_cast<uint16_t>(menu::Flag::ChainTargetsMismatch)) {
				mismatches++;
			}
		}
		expect(mismatches).to_equal(size_t(0));
	});
});

describe tables_2_patch_cables("scenes::menu batch-2 tables: the patch-cable items", $ {
	it("declares the three items with the legacy globals' own names, and none of them a placeholder", _ {
		Rigged& f = fixture();
		(void)f;
		struct Case {
			const Node* node;
			::MenuItem* item;
			const menu::CustomItem* expected;
		};
		const Case cases[] = {
		    {&nodes::patch_cables, &patchCablesMenu, &nodes::kPatchCablesItem},
		    {&nodes::vibrato, &vibratoMenu, &nodes::kVibratoItem},
		    {&nodes::sidechain_volume, &sidechainVolumeShortcutMenu, &nodes::kSidechainVolumeItem},
		};
		for (const Case& c : cases) {
			expect(text_of(c.node->name)).to_equal(std::string{c.item->getName()});
			expect(c.node->kind == menu::Kind::Custom).to_be_true();
			expect(c.node->custom == c.expected).to_be_true();
			expect(c.node->custom->is_placeholder()).to_be_false();
		}
		// The two `Fixed` items differ ONLY in their baked-in param and source, so a swapped pair
		// would be invisible to every check above. This is what makes them distinguishable: each
		// item's own `on_focus` installs its pair, and the two pairs are the ones `menus.cpp`
		// constructs the legacy globals with (`:806-807`, `:1725-1726`).
		menu::MenuState vib{};
		nodes::kVibratoItem.on_focus(fixture().ctx, vib);
		expect(int(vib.patching_param)).to_equal(int(deluge::modulation::params::LOCAL_PITCH_ADJUST));
		expect(int(vib.patch_source)).to_equal(int(PatchSource::LFO_GLOBAL_1));
		menu::MenuState duck{};
		nodes::kSidechainVolumeItem.on_focus(fixture().ctx, duck);
		expect(int(duck.patching_param)).to_equal(int(deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND));
		expect(int(duck.patch_source)).to_equal(int(PatchSource::SIDECHAIN));
	});

	it("chains exactly where the legacy items' own selectButtonPress chains", _ {
		// Each side driven from its OWN state: legacy from `soundEditor.patchingParamSelected` plus
		// the two `source_selection` singletons' `s` members, the port from a `MenuState` this
		// example fills in itself. A port hook that answered from legacy's globals -- or one that
		// answered nothing -- would not survive the comparison.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		soundEditor.patchingParamSelected = deluge::modulation::params::LOCAL_LPF_FREQ;
		legacy::source_selection::regularMenu.s = PatchSource::ENVELOPE_1;
		legacy::source_selection::rangeMenu.s = PatchSource::LFO_GLOBAL_1;

		menu::MenuState st{};
		st.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		st.patch_source = PatchSource::ENVELOPE_1;
		st.patch_range_source = PatchSource::LFO_GLOBAL_1;

		struct Case {
			const Node* node;
			::MenuItem* item;
		};
		const Case cases[] = {
		    {&nodes::source_selection_regular, &legacy::source_selection::regularMenu},
		    {&nodes::patch_cable_strength_regular, &legacy::patch_cable_strength::regularMenu},
		    {&nodes::source_selection_range, &legacy::source_selection::rangeMenu},
		    {&nodes::patch_cable_strength_range, &legacy::patch_cable_strength::rangeMenu},
		    // The two `Fixed` items are `PatchCableStrength`s, so they chain the same way.
		    {&nodes::vibrato, &vibratoMenu},
		    {&nodes::sidechain_volume, &sidechainVolumeShortcutMenu},
		};
		size_t compared = 0;
		for (const Case& c : cases) {
			const Node* want = chained_node_for(c.item->selectButtonPress());
			const Node* got = c.node->select_chain != nullptr ? c.node->select_chain(f.ctx, st) : nullptr;
			expect(chain_name(got)).to_equal(chain_name(want));
			expect(got != nullptr && chain_targets_contain(*c.node, got)).to_be_true();
			compared++;
		}
		expect(compared).to_equal(std::size(cases));
		// The 53 `PatchedParam` nodes all take `PatchedParam::selectButtonPress`'s non-shift arm
		// (`gui/menu_item/patched_param.cpp:32-38`), which is unconditional: always the regular
		// source picker. Checked against the legacy answer too, through one representative item --
		// `lpfFreqMenu`, a `patched_param::Integer`.
		::MenuItem* lpf_item = nullptr;
		for (const Pair& pr : tree().pairs) {
			if (pr.node == &nodes::lpf_freq) {
				lpf_item = pr.item;
			}
		}
		expect(lpf_item != nullptr).to_be_true();
		const Node* lpf_want = chained_node_for(lpf_item->selectButtonPress());
		expect(chain_name(nodes::lpf_freq.select_chain(f.ctx, st))).to_equal(chain_name(lpf_want));
		expect(chain_name(lpf_want)).to_equal(std::string{"source_selection_regular"});
	});

	it("chains every PATCHED node, and only those", _ {
		// E2 task 3 measured the affected set by `dynamic_cast<PatchedParam*>` over a lockstep walk
		// and found it to be **exactly** the nodes with `bind.param.kind == PATCHED`, 53 of them.
		// That equality is what `with_patch_chain` is applied on, so it is what is pinned here:
		// every PATCHED node chains to `source_selection_regular`, and no node that chains there is
		// anything else.
		size_t patched = 0;
		size_t chained_to_picker = 0;
		std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
		std::set<const Node*> seen;
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
			const bool is_patched = n->bind.param.kind == deluge::modulation::params::Kind::PATCHED;
			const bool to_picker = chain_targets_contain(*n, &nodes::source_selection_regular);
			if (is_patched) {
				patched++;
			}
			if (to_picker) {
				chained_to_picker++;
			}
			// Named rather than counted, so a failure says which node lost (or gained) its chain.
			expect(text_of(n->name) + (is_patched ? " PATCHED" : " -")).to_equal(text_of(n->name)
			                                                                    + (to_picker ? " PATCHED" : " -"));
		}
		expect(patched).to_equal(size_t(53));
		expect(chained_to_picker).to_equal(size_t(53));
	});

	it("reads and writes a patch cable's strength where the legacy page reads and writes it", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);

		// A cable from ENVELOPE_1 to LPF frequency, created by the port's own write path (which is
		// `getAutoParamFromId(..., allowCreation=true)` -- `patch_cable_strength.cpp:203-220`).
		ParamDescriptor learn;
		learn.setToHaveParamAndSource(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(menu::bindings::patch_cable_write(f.ctx, learn, 1234)).to_be_true();

		// Legacy reads it through its own globals ...
		soundEditor.patchingParamSelected = deluge::modulation::params::LOCAL_LPF_FREQ;
		legacy::source_selection::regularMenu.s = PatchSource::ENVELOPE_1;
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		const int32_t legacy_value = legacy::patch_cable_strength::regularMenu.getValue();

		// ... and the port through a `MenuState` filled in independently of them.
		menu::MenuState st{};
		st.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		st.patch_source = PatchSource::ENVELOPE_1;
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, st);
		expect(st.edit_int).to_equal(legacy_value);
		expect(st.edit_int).to_equal(int32_t(1234));
		expect(int(st.decimal_pos)).to_equal(2); // `getDefaultEditPos()` (`patch_cable_strength.h:39`)

		// The learn descriptor is the legacy page's own `getLearningThing()`.
		expect(nodes::kPatchCableStrengthRegularItem.learn_descriptor(f.ctx, st).data)
		    .to_equal(legacy::patch_cable_strength::regularMenu.getLearningThing().data);

		// And the write, from an independent sentinel: the cable is moved somewhere that is NOT the
		// value about to be written, the port's `commit` writes, and the result is read back through
		// **legacy's** reader. A `commit` that did nothing would leave the sentinel behind.
		//
		// The legacy *write* path is not driven, for the reason this file already records for
		// `ParamRef` nodes: with no root UI open, `PatchCableStrength::writeCurrentValue` resolves
		// `soundEditor.getCurrentModelStack` down the `!rootUIIsClipMinderScreen()` arm
		// (`sound_editor.cpp:1986`) and asks the *song's* param manager for a patch-cable collection
		// it does not have -- which is `FREEZE_WITH_ERROR("E412")`, i.e. a null-`display` segfault in
		// the host build. `bindings_spec` covers the model-stack half directly.
		expect(menu::bindings::patch_cable_write(f.ctx, learn, -2500)).to_be_true(); // sentinel
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		expect(legacy::patch_cable_strength::regularMenu.getValue()).to_equal(int32_t(-2500));

		st.edit_int = 4321;
		nodes::kPatchCableStrengthRegularItem.commit(f.ctx, st);
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		expect(legacy::patch_cable_strength::regularMenu.getValue()).to_equal(int32_t(4321));
	});

	it("opens the strength page the selected cable needs, and installs that cable's own sources", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		PatchCableSet* set = f.ctx.params->getPatchCableSetAllowJibberish();
		expect(set != nullptr).to_be_true();

		// Two cables: one to a bare param, one to another cable's depth. `PatchCables::selectButtonPress`
		// picks a different strength page for each (`patch_cables.cpp:181-190`).
		ParamDescriptor plain;
		plain.setToHaveParamAndSource(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(menu::bindings::patch_cable_write(f.ctx, plain, 1000)).to_be_true();
		ParamDescriptor ranged;
		ranged.setToHaveParamAndTwoSources(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1,
		                                   PatchSource::LFO_GLOBAL_1);
		expect(menu::bindings::patch_cable_write(f.ctx, ranged, 900)).to_be_true();
		expect(set->numPatchCables >= 2).to_be_true();

		size_t compared = 0;
		for (int32_t i = 0; i < set->numPatchCables; i++) {
			// Legacy is driven from its own cursor member, the port from its own `ListState` -- two
			// independent sentinels for the same choice.
			patchCablesMenu.currentValue = i;
			const Node* want = chained_node_for(patchCablesMenu.selectButtonPress());

			menu::MenuState st{};
			st.depth = 0;
			st.path[0] = &nodes::patch_cables;
			st.list[0].cursor = static_cast<int16_t>(i);
			const Node* got = nodes::patch_cables.select_chain(f.ctx, st);
			expect(chain_name(got)).to_equal(chain_name(want));
			expect(got != nullptr && chain_targets_contain(nodes::patch_cables, got)).to_be_true();

			// And the state legacy's own `selectButtonPress` installed on the way out -- which the
			// port installs on the way *in*, from the strength page's `on_focus`.
			menu::MenuState arrived = st;
			arrived.depth = 1;
			arrived.path[1] = got;
			const auto* item = static_cast<const nodes::PatchCableStrengthItem*>(got->custom);
			item->on_focus(f.ctx, arrived);
			expect(int(arrived.patching_param)).to_equal(int(soundEditor.patchingParamSelected));
			expect(int(arrived.patch_source)).to_equal(int(legacy::source_selection::regularMenu.s));
			if (!set->patchCables[i].destinationParamDescriptor.isJustAParam()) {
				expect(int(arrived.patch_range_source)).to_equal(int(legacy::source_selection::rangeMenu.s));
			}
			compared++;
		}
		expect(compared >= size_t(2)).to_be_true();

		// With no cable under the cursor at all, legacy falls through to `MenuItem::selectButtonPress`
		// -- "there were no items ... take them back there" (`patch_cables.cpp:171-175`) -- which is
		// `nullptr`, i.e. ascend.
		patchCablesMenu.currentValue = set->numPatchCables;
		expect(patchCablesMenu.selectButtonPress() == nullptr).to_be_true();
		menu::MenuState empty{};
		empty.list[0].cursor = static_cast<int16_t>(set->numPatchCables);
		expect(nodes::patch_cables.select_chain(f.ctx, empty) == nullptr).to_be_true();
	});
});

describe tables_2_pixels("scenes::menu batch-2 tables: pixels", $ {
	it("renders the sound-editor root list", _ {
		Rigged& f = fixture();
		deluge::notify::dirty().drain();
		Rig r;
		r.open(nodes::sound_root, f.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/menu_sound_root_48.pbm", r.fake.pbm(0));
	});

	it("renders the sound-editor root list on a 64-row panel", _ {
		Rigged& f = fixture();
		deluge::notify::dirty().drain();
		Rig r{board_64(), profile_64()};
		r.open(nodes::sound_root, f.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/menu_sound_root_64.pbm", r.fake.pbm(0));
	});

	it("renders the mod-matrix list and the cable-strength page, on both shipped panel geometries", _ {
		// Both geometries for both pages, because neither screen has a golden anywhere else and the
		// row arithmetic behind them (`menu_rect`, `title`, `decimal_value`) is panel-dependent --
		// task 8 shipped a 64-row bug for exactly this reason.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		ParamDescriptor plain;
		plain.setToHaveParamAndSource(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(menu::bindings::patch_cable_write(f.ctx, plain, 1234)).to_be_true();

		deluge::notify::dirty().drain();
		{
			Rig r;
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_patch_cables_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			expect_golden(self, "../ui/menu/menu_patch_cables_64.pbm", r.fake.pbm(0));
		}
		// The strength page, reached the way a user reaches it: a select press on the cable list.
		{
			Rig r;
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect(menu_scene().focused() == &nodes::patch_cable_strength_regular).to_be_true();
			expect_golden(self, "../ui/menu/menu_cable_strength_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect_golden(self, "../ui/menu/menu_cable_strength_64.pbm", r.fake.pbm(0));
		}
	});

	it("renders the source picker the 53 PatchedParam nodes chain to, on both panels", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		deluge::notify::dirty().drain();
		{
			Rig r;
			r.open(nodes::lpf_freq, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect(menu_scene().focused() == &nodes::source_selection_regular).to_be_true();
			expect_golden(self, "../ui/menu/menu_source_selection_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::lpf_freq, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect_golden(self, "../ui/menu/menu_source_selection_64.pbm", r.fake.pbm(0));
		}
	});
});
describe tables_2_writes("scenes::menu batch-2 tables: writes", $ {
	it("leaves the model where the legacy item leaves it, for every set hook", _ {
		// Review round 1, I5. Every batch-2 `set` hook is driven here: for three values spanning the
		// node's range, each write path is run **from its own copy of a common starting state**
		// (`probe_write`) and the two results are compared through *both* readers (the port's
		// `bindings::read` and the legacy `readCurrentValue()`). Disagreement on either observation
		// means the two paths put different state in the model.
		//
		// Fix round 1, I2: this used to run legacy first and the port second and assert the port
		// changed nothing -- which a port hook that stores nothing also satisfies, because legacy had
		// already moved the model to `v`. Starting each path from a sentinel that is not `v` is what
		// makes a no-op hook visible.
		//
		// `ParamRef` nodes are deliberately not here: legacy's `UnpatchedParam::writeCurrentValue`
		// resolves its model stack through `SoundEditor::getCurrentModelStack`, which with no root
		// UI open takes its `!rootUIIsClipMinderScreen()` arm (`sound_editor.cpp:1986`) and writes
		// the *song's* param collection where the port writes the clip's. That is the divergence
		// task 6 recorded as its concern 8; it is a property of the harness, not of the tables, and
		// `bindings_spec` already covers the param write path end to end. The next example covers
		// what is specific to this batch's param nodes instead.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t exercised = 0;
		// Fix round 2, I2: the assertion is the **set of distinct nodes driven**, derived from the
		// table itself (every node in the walk with a `set` hook, minus the named exclusions), not a
		// probe count with slack -- a count of "> 100" could absorb a node dropped from a table or a
		// relevance predicate going false in this fixture.
		std::set<const Node*> covered;
		std::set<const Node*> with_hook;
		for (const Pair& p : tree().pairs) {
			if (p.node->bind.set == nullptr || legacy_write_needs_ui(*p.node)) {
				continue;
			}
			with_hook.insert(p.node);
			const menu::IntRange r = menu::bindings::range(*p.node, f.ctx);
			for (int32_t v : probe_values(r)) {
				const std::optional<int32_t> sentinel = sentinel_for(r, v);
				if (!sentinel.has_value()) {
					continue; // a one-value range has no state to start from that is not `v`
				}
				const WriteProbe probe = probe_write(*p.node, p.item, f.ctx, v, *sentinel);
				if (!probe.ran) {
					break; // holds no value at all
				}
				expect(probe.got_port == probe.want_port).to_be_true();
				expect(probe.got_legacy == probe.want_legacy).to_be_true();
				exercised++;
				covered.insert(p.node);
			}
		}
		expect(covered.size()).to_equal(with_hook.size());
		for (const Node* n : with_hook) {
			expect(covered.contains(n)).to_be_true();
		}
		// ... each driven for all three values its range spans.
		expect(exercised).to_equal(size_t(3) * with_hook.size());
	});

	it("round-trips every param node, and raises the reverb flag on exactly the one that should", _ {
		// The param half of the coverage above, port-only for the reason that example explains,
		// plus review round 1's I2: `sidechain::Shape` is an `unpatched_param::UpdatingReverbParams`
		// (`sidechain/shape.h:6`), so its write must also set
		// `AudioEngine::mustUpdateReverbParamsBeforeNextRender` (`updating_reverb_params.h:26-29`).
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t exercised = 0;
		// Fix round 2, I2: the same treatment as the example above -- the distinct set of param
		// nodes round-tripped, sized from the walk rather than from a hand-written "> 90".
		std::set<const Node*> covered;
		std::set<const Node*> param_nodes;
		// Distinct nodes are what the coverage claim is about; `pair_probes` counts the *tree
		// positions* they were driven from, since a node reachable from two parents appears twice.
		size_t pair_probes = 0;
		for (const Pair& p : tree().pairs) {
			if (!p.node->bind.param.valid() || p.node->bind.set != nullptr || p.node->kind == menu::Kind::Custom) {
				continue;
			}
			param_nodes.insert(p.node);
			pair_probes++;
			const menu::IntRange r = menu::bindings::range(*p.node, f.ctx);
			for (int32_t v : probe_values(r)) {
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
				menu::bindings::write(*p.node, f.ctx, v);
				expect(menu::bindings::read(*p.node, f.ctx)).to_equal(v);
				expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender)
				    .to_equal(p.node->bind.updates_reverb_params);
				exercised++;
				covered.insert(p.node);
			}
		}
		expect(covered.size()).to_equal(param_nodes.size());
		for (const Node* n : param_nodes) {
			expect(covered.contains(n)).to_be_true();
		}
		expect(exercised).to_equal(size_t(3) * pair_probes);
		// Exactly one node in this tree carries the flag; the other `UpdatingReverbParams` item,
		// `sidechain::GlobalVolume` (`sidechain/volume_global.h:6`), belongs to task 8's roots.
		size_t flagged = 0;
		for (const Pair& p : tree().pairs) {
			if (p.node->bind.updates_reverb_params) {
				flagged++;
				expect(p.node == &nodes::sidechain_shape).to_be_true();
			}
		}
		expect(flagged).to_equal(size_t(1));
	});

	it("sweeps both drums of a kit exactly as legacy's affect-entire loop does", _ {
		// Review round 1, I5's second half. With AFFECT-ENTIRE held on a kit row, legacy's ~20
		// hand-written sweeps write every drum in the kit; the port's `apply_to_targets` /
		// `apply_to_arp_settings` must write the same set. Observing "the same set" generically is
		// what the two-drum fixture buys: select each drum in turn and read the node back.
		//
		// Fix round 2: this example kept the pre-I2 shape after fix round 1 touched the three
		// `*_writes` loops. It ran legacy's sweep first, restored both drums *through the port*, then
		// ran the port's sweep and compared -- so a port write hook that stored nothing left the kit
		// at legacy's result throughout and every assertion held. Now each sweep starts from a
		// **sentinel seeded on both drums through legacy's own single-target write**, so a no-op port
		// sweep leaves `after_port` at the sentinel while `after_legacy` is at `v`.
		KitRig& k = kit_fixture();
		const auto observe = [&](const Node& node) {
			std::array<int32_t, 2> out{};
			out[0] = menu::bindings::read(node, select_drum(k, k.drum_a));
			out[1] = menu::bindings::read(node, select_drum(k, k.drum_b));
			return out;
		};
		size_t swept = 0;
		// Fix round 2, I2: the distinct set of nodes swept, against the set the fixture makes
		// sweepable (a `set` hook, not excluded, a range wider than one value, and a legacy item
		// that holds a value at all), rather than a "> 25" a dropped node could hide inside.
		std::set<const Node*> swept_nodes;
		std::set<const Node*> sweepable;
		for (const Pair& p : tree().pairs) {
			if (p.node->bind.set == nullptr || legacy_write_needs_ui(*p.node)) {
				continue;
			}
			const menu::IntRange r = menu::bindings::range(*p.node, select_drum(k, k.drum_a));
			const int32_t v = r.max;
			const std::optional<int32_t> sentinel = sentinel_for(r, v);
			if (!sentinel.has_value()) {
				continue; // a one-value range has no state to start from that is not `v`
			}
			if (legacy_value(p.item).has_value()) {
				sweepable.insert(p.node);
			}

			// Put BOTH drums at the sentinel, one at a time, through **legacy's** single-target
			// write -- deliberately not the port's, so nothing about this starting state depends on
			// the code under test.
			const auto seed_both = [&] {
				(void)select_drum(k, k.drum_a);
				if (!legacy_write(p.item, *sentinel)) {
					return false; // holds no value at all
				}
				(void)select_drum(k, k.drum_b);
				return legacy_write(p.item, *sentinel);
			};

			if (!seed_both()) {
				continue;
			}
			const MenuContext held_port = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
			menu::bindings::write(*p.node, held_port, v);
			const std::array<int32_t, 2> after_port = observe(*p.node);

			if (!seed_both()) {
				continue;
			}
			const MenuContext held_legacy = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
			(void)held_legacy; // legacy reads the sweep condition off `currentUIMode`, which `install` set
			if (!legacy_write(p.item, v)) {
				continue;
			}
			const std::array<int32_t, 2> after_legacy = observe(*p.node);

			expect(after_port[0] == after_legacy[0]).to_be_true();
			expect(after_port[1] == after_legacy[1]).to_be_true();
			swept++;
			swept_nodes.insert(p.node);
		}
		expect(swept_nodes.size()).to_equal(sweepable.size());
		for (const Node* n : sweepable) {
			expect(swept_nodes.contains(n)).to_be_true();
		}
		expect(swept).to_equal(sweepable.size());
		install(fixture().ctx, fixture().song.song);
	});

	it("asks each drum its own allowNoteTails when sweeping the kit-arp toggle", _ {
		// Review round 1, I3. Legacy's sweep (`include_in_kit_arp.h:33-45`) builds a model stack per
		// `SoundDrum` and skips the ones that cannot hold a note tail; the port must too, or the two
		// end up with different `includeInKitArp` across the kit. Made observable by giving drum A
		// an arpeggiator -- `Sound::allowNoteTails` returns true outright when the arp is on
		// (`sound.cpp:1922-1925`) -- while drum B keeps its sample setup's answer.
		KitRig& k = kit_fixture();
		k.drum_a->arpSettings.mode = ArpMode::ARP; // `sound.cpp:1922-1925` -- arp on, tails allowed
		k.drum_b->arpSettings.mode = ArpMode::OFF;
		k.drum_a->arpSettings.includeInKitArp = false;
		k.drum_b->arpSettings.includeInKitArp = false;

		// ... and make drum B refuse: `Sound::envelopeHasSustainEver` (`sound.cpp:4313-4321`) is
		// false when envelope 0's sustain is at the bottom of its range and its decay does not
		// exceed its release, and `allowNoteTails` returns false outright when it is (`:1928-1930`).
		NoteRow* row_b = k.song.clip->getNoteRowForDrum(k.drum_b);
		expect(row_b != nullptr).to_be_true();
		if (row_b != nullptr) {
			PatchedParamSet* params_b = row_b->paramManager.getPatchedParamSet();
			namespace mp = deluge::modulation::params;
			params_b->params[mp::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(-2147483648);
			params_b->params[mp::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(-2147483648);
			params_b->params[mp::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(2147483647);
		}

		const MenuContext ctx_a = select_drum(k, k.drum_a);
		expect(menu::bindings::drum_allows_note_tails(ctx_a, k.drum_a)).to_be_true();
		// The whole point: the two drums must disagree, or the sweep test proves nothing.
		expect(menu::bindings::drum_allows_note_tails(ctx_a, k.drum_b)).to_be_false();

		const MenuContext held = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
		menu::bindings::write(nodes::arp_include_in_kit_arp, held, 1);

		// Drum A takes the write; drum B, which cannot hold a tail, is left alone -- exactly what
		// `include_in_kit_arp.h:43-45` does, and what the context-wide test this replaces did not.
		expect(k.drum_a->arpSettings.includeInKitArp).to_be_true();
		expect(k.drum_b->arpSettings.includeInKitArp).to_be_false();

		k.drum_a->arpSettings.mode = ArpMode::OFF;
		install(fixture().ctx, fixture().song.song);
	});

	it("writes the play direction to the note row on a kit row and to the clip otherwise", _ {
		// Review round 1, I4 / ruling 4. `sequence::Direction::writeCurrentValue`
		// (`sequence/direction.h:71-76`) writes the *resolved* note row when there is one and the
		// clip when there is not; `bindings::individual_note_row` resolves the kit row's.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		const auto want_clip = static_cast<int32_t>(SequenceDirection::REVERSE);
		menu::bindings::write(nodes::sequence_direction, f.ctx, want_clip);
		expect(static_cast<int32_t>(f.song.clip->sequenceDirectionMode)).to_equal(want_clip);

		KitRig& k = kit_fixture();
		const MenuContext kit_ctx = select_drum(k, k.drum_a);
		expect(k.song.clip->affectEntire).to_be_false(); // the precondition of the kit-row arm
		NoteRow* row = k.song.clip->getNoteRowForDrum(k.drum_a);
		expect(row != nullptr).to_be_true();
		const auto want_row = static_cast<int32_t>(SequenceDirection::PINGPONG);
		const auto clip_before = static_cast<int32_t>(k.song.clip->sequenceDirectionMode);
		menu::bindings::write(nodes::sequence_direction, kit_ctx, want_row);
		if (row != nullptr) {
			expect(static_cast<int32_t>(row->sequenceDirectionMode)).to_equal(want_row);
		}
		expect(static_cast<int32_t>(k.song.clip->sequenceDirectionMode)).to_equal(clip_before);
		install(f.ctx, f.song.song);
	});

	it("drives the port hook of every node the legacy write comparison excludes", _ {
		// Fix round 2, I1. `kLegacyWriteNeedsUi` above stands the **legacy** call down; it must not
		// stand the port's down with it. Each of the four hooks is driven here on its own and its
		// persisted effect asserted -- including the two arpeggiator ones, whose real work
		// (`silence_for_arp_toggle`, `updatePresetFromCurrentSettings`,
		// `updateSettingsFromCurrentPreset`, `flagForceArpRestart`, and the kit-wide sweep over
		// every `Drum` rather than every `SoundDrum`) is invisible to `bindings::read`.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t covered = 0;

		{ // --- synth_mode --------------------------------------------------------------------
			const SynthMode before = f.ctx.sound->synthMode;
			menu::bindings::write(nodes::synth_mode, f.ctx, static_cast<int32_t>(SynthMode::RINGMOD));
			expect(f.ctx.sound->synthMode == SynthMode::RINGMOD).to_be_true();
			menu::bindings::write(nodes::synth_mode, f.ctx, static_cast<int32_t>(SynthMode::SUBTRACTIVE));
			expect(f.ctx.sound->synthMode == SynthMode::SUBTRACTIVE).to_be_true();
			f.ctx.sound->setSynthMode(before, f.ctx.song);
			covered++;
		}

		{ // --- mod_fx_type -------------------------------------------------------------------
			const ModFXType before = f.ctx.mod->modFXType_;
			menu::bindings::write(nodes::mod_fx_type, f.ctx, static_cast<int32_t>(ModFXType::NONE));
			expect(f.ctx.mod->modFXType_ == ModFXType::NONE).to_be_true();
			menu::bindings::write(nodes::mod_fx_type, f.ctx, static_cast<int32_t>(ModFXType::FLANGER));
			expect(f.ctx.mod->modFXType_ == ModFXType::FLANGER).to_be_true();
			(void)f.ctx.mod->setModFXType(before);
			covered++;
		}

		// `silence_for_arp_toggle` only fires on a clip that is actually sounding
		// (`sound_arp.cpp:94`, legacy `mode.h:30`), and `song_fixture.h` never makes its clip the
		// output's active one -- nothing in the fixture plays. Arranged here, and put back.
		Clip* const saved_active = OutputPeek::active_clip_of(*f.ctx.output);
		OutputPeek::active_clip_of(*f.ctx.output) = f.ctx.clip;
		expect(f.ctx.clip->isActiveOnOutput()).to_be_true();

		{ // --- arp_mode: the silence block and the preset re-derivation ----------------------
			ArpeggiatorSettings* arp = f.ctx.arp;
			expect(arp != nullptr).to_be_true();
			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::DOWN;
			arp->noteMode = ArpNoteMode::DOWN;
			// `Sound::allNotesOff` clears this first thing (`sound.cpp:1761-1763`): the one effect
			// of `silence_for_arp_toggle`'s synth arm that lands in readable model state.
			f.ctx.sound->invertReversed = true;

			menu::bindings::write(nodes::arp_mode, f.ctx, static_cast<int32_t>(ArpMode::ARP));

			expect(arp->mode == ArpMode::ARP).to_be_true();
			// DOWN/DOWN is `ArpPreset::DOWN` (`arpeggiator.cpp:1936-1938`) -- false unless
			// `updatePresetFromCurrentSettings()` ran.
			expect(arp->preset == ArpPreset::DOWN).to_be_true();
			expect(f.ctx.sound->invertReversed).to_be_false();
			covered++;
		}

		{ // --- arp_preset: the settings re-derivation, the restart flag, the silence block ----
			ArpeggiatorSettings* arp = f.ctx.arp;
			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::RANDOM;
			arp->noteMode = ArpNoteMode::RANDOM;
			arp->flagForceArpRestart = false;
			f.ctx.sound->invertReversed = true;

			menu::bindings::write(nodes::arp_preset, f.ctx, static_cast<int32_t>(ArpPreset::DOWN));

			expect(arp->preset == ArpPreset::DOWN).to_be_true();
			// `updateSettingsFromCurrentPreset()` (`arpeggiator.cpp:1971-2005`) is the only thing
			// that moves these three.
			expect(arp->mode == ArpMode::ARP).to_be_true();
			expect(arp->octaveMode == ArpOctaveMode::DOWN).to_be_true();
			expect(arp->noteMode == ArpNoteMode::DOWN).to_be_true();
			expect(arp->flagForceArpRestart).to_be_true();
			expect(f.ctx.sound->invertReversed).to_be_false();

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->flagForceArpRestart = false;
			covered++;
		}

		OutputPeek::active_clip_of(*f.ctx.output) = saved_active;
		expect(covered).to_equal(std::size(kLegacyWriteNeedsUi));

		// --- and the kit sweep the generic `*_writes` loop cannot reach for these two ----------
		// `apply_to_arp_settings` (`sound_arp.cpp:56-68`) visits every `Drum`, not every
		// `SoundDrum` the way `apply_to_targets` does -- so the affect-entire sweep for the arp
		// items needs its own check. Both drums must come out fully re-derived, not merely with
		// `preset` stored.
		KitRig& k = kit_fixture();
		const MenuContext held = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
		for (SoundDrum* drum : {k.drum_a, k.drum_b}) {
			drum->arpSettings.mode = ArpMode::OFF;
			drum->arpSettings.preset = ArpPreset::OFF;
			drum->arpSettings.octaveMode = ArpOctaveMode::RANDOM;
			drum->arpSettings.noteMode = ArpNoteMode::RANDOM;
			drum->arpSettings.flagForceArpRestart = false;
		}
		menu::bindings::write(nodes::arp_preset, held, static_cast<int32_t>(ArpPreset::UP));
		for (SoundDrum* drum : {k.drum_a, k.drum_b}) {
			expect(drum->arpSettings.preset == ArpPreset::UP).to_be_true();
			expect(drum->arpSettings.mode == ArpMode::ARP).to_be_true();
			expect(drum->arpSettings.octaveMode == ArpOctaveMode::UP).to_be_true();
			expect(drum->arpSettings.noteMode == ArpNoteMode::UP).to_be_true();
			expect(drum->arpSettings.flagForceArpRestart).to_be_true();
			drum->arpSettings.mode = ArpMode::OFF;
			drum->arpSettings.preset = ArpPreset::OFF;
			drum->arpSettings.flagForceArpRestart = false;
		}
		install(f.ctx, f.song.song);
	});
});
// clang-format on

/// Seven `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead.
int tables_2_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(tables_2_shape, tables_2_values, tables_2_validation, tables_2_chaining,
	                          tables_2_patch_cables, tables_2_pixels, tables_2_writes)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
