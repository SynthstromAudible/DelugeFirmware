/// @file
/// @brief `MenuScene` -- the menu interpreter -- driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, with the model-backed `MenuContext` the song fixture resolves.
///
/// The tree under test is declared here rather than taken from `src/scenes/menu/tables/`: Tasks
/// 6-8 own the real tables, and this task's subject is the interpreter, not the data. It is
/// deliberately shaped like the settings root -- a vertical submenu holding one horizontal
/// submenu, an `Int`, a `Decimal`, an `Enum`, a `Toggle` and one `range_dependent` param -- so
/// every per-kind leaf helper and both layouts are exercised by one root.
///
/// @note The spec may include `gui/` (for `l10n::String` ids); the library may not. Same rule
///       `bindings_spec.cpp` runs under.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "model/sync.h" // NUM_SYNC_VALUES
#include "modulation/params/param.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <version.h> // kFirmwareVersionString -- the real firmware_version CustomItem's own draw

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::ButtonSet;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

// The real global `battery_level`'s `CustomItem` reads (`deluge.cpp:91`) -- declared bare and at
// global scope, exactly as `settings.cpp` and `sound_editor.cpp:1458` both do (a declaration
// inside this file's own anonymous namespace below would name a different, unlinked variable).
extern uint16_t batteryMV;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// @brief Per-example reset of the two process-global things a menu touches.
///
/// The real app picks a language during boot (`OLED()`'s constructor, `hid/display/oled.h:39`);
/// this spec renders text without booting, so `l10n::chosenLanguage` would otherwise be null and
/// `getView` would dereference it. And the notice queue is global and survives a `Rig`, so a
/// notification one example raised would otherwise be drained by the *next* example's first tick
/// and put an overlay scene on its stack.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// @brief An `l10n::String` as the plain `uint16_t` a `Node` carries (`node.h`: no `gui/l10n` in
///        the library, so a string id crosses as a number).
constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The tree under test. `get`/`set` leaves rather than `ParamRef` ones wherever the spec wants to
// read the committed value back without standing up a model stack; the two `param()` nodes are
// there because the horizontal page and the permission rule both need a real bound param.

int32_t g_int = 3;
int32_t g_decimal = 1234;
int32_t g_enum = 1;
bool g_toggle = false;

int32_t get_int(const MenuContext&) {
	return g_int;
}
void set_int(const MenuContext&, int32_t v) {
	g_int = v;
}
int32_t get_decimal(const MenuContext&) {
	return g_decimal;
}
void set_decimal(const MenuContext&, int32_t v) {
	g_decimal = v;
}
int32_t get_enum(const MenuContext&) {
	return g_enum;
}
void set_enum(const MenuContext&, int32_t v) {
	g_enum = v;
}
int32_t get_toggle(const MenuContext&) {
	return g_toggle ? 1 : 0;
}
void set_toggle(const MenuContext&, int32_t v) {
	g_toggle = v != 0;
}
int32_t g_sync = 0;
int32_t get_sync(const MenuContext&) {
	return g_sync;
}
void set_sync(const MenuContext&, int32_t v) {
	g_sync = v;
}

/// The two `get`/`set`-backed columns of the horizontal page, so the spec can force exact values
/// into the `Enum` and `Decimal` slot renderers (and, through the latter, into `format_decimal`).
int32_t g_lpf_mode = 1;
int32_t g_morph = 1234;

int32_t get_lpf_mode(const MenuContext&) {
	return g_lpf_mode;
}
void set_lpf_mode(const MenuContext&, int32_t v) {
	g_lpf_mode = v;
}
int32_t get_morph(const MenuContext&) {
	return g_morph;
}
void set_morph(const MenuContext&, int32_t v) {
	g_morph = v;
}

constexpr uint16_t kEnumNames[] = {sid(String::STRING_FOR_DISABLED), sid(String::STRING_FOR_ENABLED),
                                   sid(String::STRING_FOR_OFF)};

constexpr Node lpf_freq =
    menu::param(sid(String::STRING_FOR_LPF_FREQUENCY),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ},
                deluge::ui::oled::Hint::Lpf);
constexpr Node lpf_res =
    menu::param(sid(String::STRING_FOR_RESONANCE),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_RESONANCE},
                deluge::ui::oled::Hint::Bar);
/// The horizontal page's other two columns: an `Enum` and a `Decimal`, so the page exercises all
/// three slot renderers `columns` can be handed -- `meter_column` (FREQ/RES), `text_column` from a
/// name (MODE) and `text_column` from `format_decimal` (MORPH).
constexpr Node lpf_mode = menu::enumeration(sid(String::STRING_FOR_MODE), kEnumNames, get_lpf_mode, set_lpf_mode);
constexpr Node lpf_morph =
    menu::decimal_item(sid(String::STRING_FOR_MORPH), get_morph, set_morph, deluge::ui::oled::Range{-9999, 9999, 1}, 2);
constexpr const Node* lpf_children[] = {&lpf_freq, &lpf_res, &lpf_mode, &lpf_morph};
constexpr Node lpf_page = menu::submenu(sid(String::STRING_FOR_LPF), lpf_children, menu::Layout::Horizontal);

constexpr Node int_leaf =
    menu::int_item(sid(String::STRING_FOR_VOLUME_LEVEL), get_int, set_int, deluge::ui::oled::Range{0, 50, 1});
constexpr Node decimal_leaf =
    menu::decimal_item(sid(String::STRING_FOR_TEMPO), get_decimal, set_decimal, deluge::ui::oled::Range{0, 9999, 1}, 2);
constexpr Node enum_leaf = menu::enumeration(sid(String::STRING_FOR_MODE), kEnumNames, get_enum, set_enum);
constexpr Node toggle_leaf = menu::toggle_item(sid(String::STRING_FOR_MONOPHONIC), get_toggle, set_toggle);
/// A `Kind::SyncLevel` leaf, so the interpreter's one arm for it (task 1 of E2) is exercised
/// through a real tree rather than only through `validate.cpp`'s tables. Reuses
/// `nodes::kSyncLevelRange` (`sound_lfo.h:66`) rather than spelling `NUM_SYNC_VALUES` again.
constexpr Node sync_leaf = menu::sync_level_item(sid(String::STRING_FOR_SYNC), sid(String::STRING_FOR_DELAY_SYNC),
                                                 get_sync, set_sync, nodes::kSyncLevelRange);
/// The range-dependent item spec E §4.1's `MustSelectRange` arm fires on.
constexpr Node transpose_leaf = menu::with_range_dependent(
    menu::param(sid(String::STRING_FOR_TRANSPOSE),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ}));

/// @brief A `CustomItem` that reports whether/how many times it saw `Back`, and either consumes it
///        (returning `Pop`) or lets it fall through -- the two halves of task 2's precedence claim.
///
/// Not added to `settings_children`: these are opened directly via `menu_scene().open(...)`
/// (`Rig::open` already gave the tree a real `MenuContext` to reuse), so nothing here can move an
/// existing recorded golden.
struct CountingCustom final : menu::CustomItem {
	mutable int handled = 0;
	mutable int focused = 0;
	bool consumes;
	explicit CountingCustom(bool consumes_back) : consumes(consumes_back) {}
	void render(deluge::ui::oled::OledFrame&, const MenuContext&, menu::MenuState&) const override {}
	Result handle(const UiEvent& e, const UiContext&, const MenuContext&, menu::MenuState&) const override {
		if (e.kind == UiEvent::Kind::Button && e.button == ButtonId::Back && e.on) {
			handled++;
			if (!consumes) {
				return Result::unhandled();
			}
			Result r{.handled = true};
			(void)r.actions.push_back({.kind = deluge::ui::shell::Action::Kind::Pop});
			return r;
		}
		return Result::unhandled();
	}
	// `on_focus` (fix round 2): counts focus transitions, so the spec can prove it fires once per
	// `open()`/descent, not once per `render_oled` tick.
	void on_focus(const MenuContext&, menu::MenuState&) const override { focused++; }
};
CountingCustom g_custom_consumes{true};
CountingCustom g_custom_passes_through{false};
constexpr Node custom_consumes_leaf = menu::custom(sid(String::STRING_FOR_OK), &g_custom_consumes);
constexpr Node custom_passes_through_leaf = menu::custom(sid(String::STRING_FOR_OK), &g_custom_passes_through);

// ---------------------------------------------------------------------------------------------
// The `select_chain` leaves. Both are appended to `settings_children` *last* (see the note there),
// so no recorded golden moves.

/// The second hop of the chain: the page `chain_target_leaf` itself opens, so a spec can prove the
/// interpreter follows a chain *from* a page it arrived at by chaining (legacy's
/// `OctaveModeToNoteMode` -> `arpNoteModeFromOctaveModeMenu`, `octave_mode.h:90`).
constexpr Node chain_target_hop2 =
    menu::int_item(sid(String::STRING_FOR_MORPH), get_int, set_int, deluge::ui::oled::Range{0, 50, 1});
constexpr const Node* chain_target_leaf_targets[] = {&chain_target_hop2};
const Node* chain_target_leaf_chain(const MenuContext&, const menu::MenuState&) {
	return &chain_target_hop2;
}

/// The page a select press on `chain_source_leaf` opens -- deliberately NOT a child of anything,
/// the shape legacy's `arpOctaveModeToNoteModeMenu` and `cvSubmenu` have
/// (`gui/ui/menus.cpp:511-521`, `:1232`): reachable through `Node::chain_targets` and nothing else.
constexpr Node chain_target_leaf = menu::with_select_chain(
    menu::int_item(sid(String::STRING_FOR_RESONANCE), get_int, set_int, deluge::ui::oled::Range{0, 50, 1}),
    chain_target_leaf_chain, chain_target_leaf_targets);

/// Value-dependent, like `arp_preset`: only an odd value chains, everything else ascends. The two
/// branches are what make the interpreter's `select_chain != nullptr ? ... : nullptr` arm testable
/// in both directions rather than only the interesting one.
constexpr const Node* chain_source_targets[] = {&chain_target_leaf};
const Node* chain_source_chain(const MenuContext&, const menu::MenuState&) {
	return (g_int % 2) != 0 ? &chain_target_leaf : nullptr;
}
constexpr Node chain_source_leaf = menu::with_select_chain(
    menu::int_item(sid(String::STRING_FOR_PAN), get_int, set_int, deluge::ui::oled::Range{0, 50, 1}),
    chain_source_chain, chain_source_targets);

/// A three-deep chain, so `save_state`/`restore_state` can be round-tripped at the depth the brief
/// asks for. Appended *last* so the visible window at cursor 0 -- and therefore the recorded root
/// goldens -- is unchanged.
constexpr Node deep_leaf =
    menu::int_item(sid(String::STRING_FOR_PAN), get_int, set_int, deluge::ui::oled::Range{0, 50, 1});
constexpr const Node* deep_inner_children[] = {&deep_leaf};
constexpr Node deep_inner = menu::submenu(sid(String::STRING_FOR_LPF), deep_inner_children);
constexpr const Node* deep_outer_children[] = {&deep_inner};
constexpr Node deep_outer = menu::submenu(sid(String::STRING_FOR_FILTERS), deep_outer_children);

// `sync_leaf` is appended last, like `deep_outer` before it, so the visible window at cursor 0 --
// and therefore the recorded root goldens -- is unchanged (`render_vertical` draws no scrollbar,
// so even the total count moving from 7 to 8 does not move a pixel there). `chain_source_leaf`
// follows it, for the same reason and with the same effect.
constexpr const Node* settings_children[] = {&lpf_page,   &int_leaf,    &decimal_leaf,
                                             &enum_leaf,  &toggle_leaf, &transpose_leaf,
                                             &deep_outer, &sync_leaf,   &chain_source_leaf};
constexpr Node settings_root = menu::submenu(sid(String::STRING_FOR_SETTINGS), settings_children);

/// The synthetic tree above **and** the real sound root: the shortcut examples below press real
/// pads on the real `Root::Sound` table (task 9 deleted the installable stub table), and a jump
/// target has to have been given a node id or `handle()` refuses to offer the jump at all.
constexpr const Node* kRoots[] = {&settings_root, &nodes::sound_root};

/// @brief Two real cells of `paramShortcutsForSounds`, and the second-layer cell that shares one of
///        their pads (`gui/ui/menus.cpp:2174`, `:2194`).
/// @{
constexpr uint8_t kLpfFreqX = 8, kLpfFreqY = 7; ///< `&lpfFreqMenu`
constexpr uint8_t kEnv1RelX = 8, kEnv1RelY = 0; ///< `&env1ReleaseMenu`, second layer `&env3ReleaseMenu`
constexpr uint8_t kHpfFreqX = 9, kHpfFreqY = 7; ///< `&hpfFreqMenu`
/// @}

// ---------------------------------------------------------------------------------------------

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry. Ruling: the 48/64 split comes from `BoardProfile::style_by_index`, not
///        from a switch inside the scene.
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// Board encoder indices (`kDelugeEncoders`, `shell/profiles/deluge.cpp`): 3 = Select,
/// 4 = GoldUpper, 5 = GoldLower.
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kGoldUpper = 4;
/// Matrix coordinates (`kMatrix`, `shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;
/// The four instrument-button columns, and the two page-switch buttons -- all six of
/// `HorizontalMenu::buttonAction`'s (`horizontal_menu.cpp:70-107`).
constexpr uint8_t kSynthX = 5, kSynthY = 0;
constexpr uint8_t kKitX = 5, kKitY = 1;
constexpr uint8_t kMidiX = 5, kMidiY = 2;
constexpr uint8_t kCvX = 5, kCvY = 3;
constexpr uint8_t kCrossScreenX = 6, kCrossScreenY = 2;
constexpr uint8_t kScaleX = 6, kScaleY = 0;

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs (E3 task 10).
	///
	/// `MenuScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, exactly the cross-contamination
	/// `recorder_scene_spec.cpp`'s `~Rig` was written to stop (a leaked recorder there made the next
	/// `on_enter` refuse outright and killed 13 of 16 examples on one mutant's first run). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	/// @brief Resolve a `MenuContext` from the fixture's synth clip and open @p root on it.
	MenuContext open(const Node& root) {
		const MenuContext ctx =
		    menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	/// @brief One raw encoder-edge event on the board's encoder \p index.
	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	/// @brief \p detents clicks of the (detented) select encoder.
	///
	/// A `EncoderKind::Detented` encoder takes `LoweringConfig::edges_per_detent` (2) A-pin edges
	/// per click (`shell/input_lowering.h:28`), so a spec that wants one detent sends two edges.
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	/// @brief \p delta detents of a gold knob.
	///
	/// The gold knobs are `EncoderKind::Accelerating`, so the raw edge count IS the magnitude and
	/// no `edges_per_detent` division applies. `InputLowering::action_gold_turn`
	/// (`shell/input_lowering.cpp:209-231`) additionally swallows the first turn after a *pause*
	/// longer than `short_press_ms` (500 ms) as a possible accidental wiggle -- but
	/// `last_turn_ms_` starts at 0, so every clock this spec uses (all under 500 ms) already
	/// counts as "turned recently" and one event is one acted-on turn. Keep spec clocks under
	/// 500 ms, or the first gold turn will be silently swallowed.
	void gold(uint8_t index, int16_t delta, uint32_t at_ms) { edges(index, delta, at_ms); }
	/// @brief One button press/release at matrix (\p x, \p y).
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One pad press/release at absolute grid (\p x, \p y).
	void pad(uint8_t x, uint8_t y, uint8_t velocity, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(velocity)}, at_ms);
	}
	/// @brief Enter the submenu the cursor is on (a select-encoder press edge).
	void enter(uint32_t at_ms) { button(kSelectEncButtonX, kSelectEncButtonY, true, at_ms); }
	/// @brief What the grid holds at (\p x, \p y) after the last rendered tick.
	RGB pad_colour(uint8_t x, uint8_t y) {
		const auto& g = fake.grid().grid_storage;
		return g.has_value() ? g->at(x, y) : RGB{};
	}
	/// @brief `InputClaims::double_claim` for the tick that just ran.
	///
	/// Both render passes share the shell's one `FrameContext` (`Shell::lowering().ctx()`), and
	/// `begin_tick` clears the flag, so this is the answer for the most recent tick.
	bool double_claimed() { return shell.lowering().ctx().claims.double_claim; }
};

int depth_of() {
	return menu_scene().state().depth;
}
int cursor_of(int level) {
	return menu_scene().state().list[level].cursor;
}

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	ButtonSet held{};
	deluge::ui::PadMask pads{};
	UiContext ctx;
	explicit DirectCtx(bool shift = false, uint32_t now_ms = 0)
	    : ctx{held, pads, shift, false, now_ms, deluge_profile()} {}
};

} // namespace

// clang-format off
describe menu_scene_composition("scenes::menu::MenuScene composition", $ {
	it("is a grid overlay that neither occludes, dims nor lets the OLED through", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		const deluge::ui::shell::Scene& s = menu_scene();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		expect(s.dims_grid()).to_be_false();
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_false();
		expect(s.input_transparent()).to_be_false();
		expect(static_cast<int>(s.kind())).to_equal(static_cast<int>(SceneKind::FirstApp));
	});

	it("registers itself as the one application scene, at SceneKind::FirstApp", _ {
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		expect(reg.find(SceneKind::FirstApp) == &menu_scene()).to_be_true();
		expect(reg.find(SceneKind::Popup) == nullptr).to_be_true();
		expect(reg.find(SceneKind::Loading) == nullptr).to_be_true();
	});

	it("greys the whole main grid for a clip-minder context, sparing the sidebar", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		// `SoundEditor::getGreyoutColsAndRows`'s INSTRUMENT_CLIP arm (`sound_editor.cpp:274`):
		// cols = 0xFFFFFFFE, i.e. every column but the rightmost sidebar one.
		expect(static_cast<uint64_t>(menu_scene().greyout_mask().cols)).to_equal(uint64_t{0xFFFFFFFEu});
		expect(static_cast<uint64_t>(menu_scene().greyout_mask().rows)).to_equal(uint64_t{0});
	});

	it("resolves on_enter through the node-id registry and refuses an unknown id", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		expect(menu_scene().on_enter(SceneId{SceneKind::FirstApp, menu::node_id(int_leaf)})).to_be_true();
		expect(menu_scene().focused() == &int_leaf).to_be_true();
		expect(menu_scene().on_enter(SceneId{SceneKind::FirstApp, 60000})).to_be_false();
		// Arg 0 means "the root this context chose". Until task 8 filled `MenuScene::roots()` there
		// was nothing to choose and this refused; now it lands on the application's own root for the
		// context the fixture resolved -- a synth clip, so `Root::Sound` -- which is a different node
		// from this spec's hand-built `settings_root`, and that is the point of the check.
		expect(menu_scene().on_enter(SceneId{SceneKind::FirstApp, 0})).to_be_true();
		expect(menu_scene().focused() == menu::MenuScene::roots()[size_t(menu::MenuContext::Root::Sound)])
		    .to_be_true();
		expect(menu_scene().focused() != &settings_root).to_be_true();
	});
});

describe menu_scene_info_and_custom("scenes::menu::MenuScene Kind::Custom input, and its two battery/firmware CustomItems (task 2)", $ {
	it("delivers input to a focused custom item, ahead of the scene's own handling", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_custom_consumes.handled = 0;
		menu_scene().open(custom_consumes_leaf, ctx);
		expect(menu_scene().focused() == &custom_consumes_leaf).to_be_true();

		// `Back` at depth 0 would otherwise be the scene's own root-close case (`Remove`,
		// `handle()`'s case (b)) -- the custom item sees it first, and its own `Pop` wins.
		DirectCtx d;
		const Result res = menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		expect(g_custom_consumes.handled).to_equal(1);
		expect(res.handled).to_be_true();
		expect(res.actions.size()).to_equal(size_t(1));
		expect(static_cast<int>(res.actions[0].kind)).to_equal(static_cast<int>(deluge::ui::shell::Action::Kind::Pop));
	});

	it("falls through to the scene's own handling when the custom item does not consume the event", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_custom_passes_through.handled = 0;
		menu_scene().open(custom_passes_through_leaf, ctx);

		DirectCtx d;
		const Result res = menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		expect(g_custom_passes_through.handled).to_equal(1); // saw the event ...
		expect(res.handled).to_be_true();                    // ... but the scene's own root Back ran
		expect(static_cast<int>(res.actions[0].kind))
		    .to_equal(static_cast<int>(deluge::ui::shell::Action::Kind::Remove));
	});

	it("firmware_version's real CustomItem draws the build string, centred under its title", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		expect(nodes::firmware_version.kind == menu::Kind::Custom).to_be_true();
		expect(nodes::firmware_version.custom != nullptr).to_be_true();
		menu_scene().open(nodes::firmware_version, ctx);

		deluge::ui::test::Rig48 rig;
		rig.begin_tick(0);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);

		// Legacy: `MenuItem::renderOLED()` (title, then `drawPixelsForOled()`) ->
		// `firmware::Version::drawPixelsForOled`'s `drawStringCentredShrinkIfNecessary(str, 22, 18, 20)`.
		rig.reference.draw_screen_title(deluge::l10n::getView(String::STRING_FOR_FIRMWARE_VER_MENU_TITLE));
		rig.reference.draw_string_centred_shrink_if_necessary(kFirmwareVersionString, 22, 18, 20);
		expect(rig.pbm()).to_equal(rig.reference_pbm());
	});

	it("battery_level's real CustomItem shows the charging suffix after a voltage rise, and not for "
	   "a flat reading", _ {
		// The coordinator's own spec for the reversal: legacy's `isCharging` (`battery/level.h:45,`
		// `57-61,71-73`) is a voltage-delta held across timer ticks on the menu item instance --
		// `BatteryLevelItem`'s `mutable` fields reproduce exactly that, which is why this is a
		// `CustomItem` rather than a stateless `Kind::Info` hook.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		expect(nodes::battery_level.kind == menu::Kind::Custom).to_be_true();
		expect(nodes::battery_level.custom != nullptr).to_be_true();
		menu_scene().open(nodes::battery_level, ctx);

		deluge::ui::test::Rig48 rig;

		// Render 1 (t=0): establishes the baseline (`checked_once_` false -> true), no charging
		// decision made yet.
		batteryMV = 3700;
		rig.begin_tick(0);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);

		// Render 2 (t=2500, >= the 2000 ms check window): same voltage -- flat, so no " CHG".
		// (3700 - 2600) * 100 / (4200 - 2600) = 68.
		batteryMV = 3700;
		rig.canvas.clear();
		rig.begin_tick(2500);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);
		rig.reference.clear();
		rig.reference.draw_screen_title(deluge::l10n::getView(String::STRING_FOR_BATTERY_LEVEL_MENU_TITLE));
		rig.reference.draw_string_centred_shrink_if_necessary("68% (3700mV)", 22, 18, 20);
		expect(rig.pbm()).to_equal(rig.reference_pbm());

		// Render 3 (t=5000, >= 2000 ms after the last check): +50 mV, well past the 5 mV threshold
		// -- charging, so " CHG" appears. (3750 - 2600) * 100 / 1600 = 71.
		batteryMV = 3750;
		rig.canvas.clear();
		rig.begin_tick(5000);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);
		rig.reference.clear();
		rig.reference.draw_screen_title(deluge::l10n::getView(String::STRING_FOR_BATTERY_LEVEL_MENU_TITLE));
		rig.reference.draw_string_centred_shrink_if_necessary("71% (3750mV) CHG", 22, 18, 20);
		expect(rig.pbm()).to_equal(rig.reference_pbm());
	});

	it("CustomItem::on_focus fires once per focus transition, not once per render (fix round 2)", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_custom_consumes.focused = 0;

		menu_scene().open(custom_consumes_leaf, ctx);
		expect(g_custom_consumes.focused).to_equal(1);

		deluge::ui::test::Rig48 rig;
		for (int i = 0; i < 3; i++) {
			rig.begin_tick(static_cast<uint32_t>(i * 100));
			menu_scene().render_oled(SurfaceRole::Main, rig.frame);
		}
		// Three renders of the same focus, still one `on_focus` call.
		expect(g_custom_consumes.focused).to_equal(1);

		// Leaving and coming back is a fresh focus transition, so it fires again.
		menu_scene().open(settings_root, ctx);
		menu_scene().open(custom_consumes_leaf, ctx);
		expect(g_custom_consumes.focused).to_equal(2);
	});

	it("battery_level's charging baseline resets on refocus, not just once for the process (fix "
	   "round 2)", _ {
		// Without `BatteryLevelItem::on_focus` resetting `checked_once_`, leaving the screen and
		// returning shortly after takes the *stale* baseline's elapsed-time gate -- `now -
		// last_check_ms_ < kChargeCheckMs` this soon after the last real check -- so `charging_`
		// stays at whatever it last was, rather than being re-established fresh for this visit.
		// This example fails (still shows " CHG") if that reset is removed.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);

		// Visit 1: baseline, then a rise past the threshold -- CHG registers.
		menu_scene().open(nodes::battery_level, ctx);
		deluge::ui::test::Rig48 rig;
		batteryMV = 3700;
		rig.begin_tick(0);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame); // baseline
		batteryMV = 3750;
		rig.canvas.clear();
		rig.begin_tick(3000);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame); // +50 mV, >= 2000 ms later -> CHG
		rig.reference.clear();
		rig.reference.draw_screen_title(deluge::l10n::getView(String::STRING_FOR_BATTERY_LEVEL_MENU_TITLE));
		rig.reference.draw_string_centred_shrink_if_necessary("71% (3750mV) CHG", 22, 18, 20);
		expect(rig.pbm()).to_equal(rig.reference_pbm());

		// Leave: focus something else entirely.
		menu_scene().open(settings_root, ctx);

		// Return, only 500 ms after the last real check (t=3000) -- too soon for a *fresh* check to
		// fire on its own, so the only way this render can show no CHG is if re-focusing itself
		// re-baselined and cleared the stale flag.
		menu_scene().open(nodes::battery_level, ctx);
		batteryMV = 3750; // flat -- unchanged since we left
		rig.canvas.clear();
		rig.begin_tick(3500);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);
		rig.reference.clear();
		rig.reference.draw_screen_title(deluge::l10n::getView(String::STRING_FOR_BATTERY_LEVEL_MENU_TITLE));
		rig.reference.draw_string_centred_shrink_if_necessary("71% (3750mV)", 22, 18, 20); // no CHG
		expect(rig.pbm()).to_equal(rig.reference_pbm());
	});

	it("GateSelectionItem's backward arm snaps the cursor to the last REAL output, not to the "
	   "off-time row it just left (task 10)", _ {
		// `gate::Selection::beginSession` restores `soundEditor.currentSourceIndex` on the way back
		// (`gate/selection.h:33-41`), and `selectButtonPress` never writes that field for the
		// minimum-off-time row -- it returns `&gateOffTimeMenu` before the assignment
		// (`gate/selection.h:44-47`). So legacy's cursor SNAPS to the last real output chosen,
		// discarding the off-time row, on every backward navigation out of it. `ascend()`'s old
		// blanket `Kind::Custom` exclusion -- "leave `MenuState::list[depth]` alone" -- gives the
		// WRONG answer here: it keeps the cursor on the off-time row itself, because nothing ever
		// moved it. `GateSelectionItem::on_focus_backward` (`nodes/dynamic.cpp`) is what fixes it,
		// and this is the case E3 task 10's `node.h` doc names as the reason a backward arm is
		// needed at all: the forward arm alone (`CustomItem::on_focus`) never fires on the way up.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = menu::context_for(*r.song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots); // the real tree: `gate_selection` lives there,
		                                               // not in this file's synthetic `settings_root`.
		menu_scene().open(nodes::gate_selection, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::gate_selection)});
		r.shell.tick(0);
		expect(cursor_of(0)).to_equal(0); // `setValue(0)`'s forward arm -- GATE OUTPUT 1.

		// GATE OUTPUT 3 (row 2), a real output: select descends to `gate_mode`.
		r.turn(2, 10);
		r.shell.tick(20);
		expect(cursor_of(0)).to_equal(2);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 30);
		r.shell.tick(40);
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &nodes::gate_mode).to_be_true();

		// Back out: no divergence yet -- row 2 is exactly what `currentSourceIndex` now holds too,
		// so "keep the cursor" and "restore from `currentSourceIndex`" agree.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 50);
		r.button(kBackX, kBackY, true, 60);
		r.shell.tick(70);
		expect(depth_of()).to_equal(0);
		expect(cursor_of(0)).to_equal(2);

		// Walk to MINIMUM OFF TIME (row 4) and select: routes to `gate_off_time`, the one page
		// `gate::Selection`'s own commit never touches.
		r.button(kBackX, kBackY, false, 80);
		r.turn(2, 90);
		r.shell.tick(100);
		expect(cursor_of(0)).to_equal(4);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 110);
		r.shell.tick(120);
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &nodes::gate_off_time).to_be_true();

		// Back out: the cursor snaps to 2 -- the last REAL output -- not to 4, the row just left.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 130);
		r.button(kBackX, kBackY, true, 140);
		r.shell.tick(150);
		expect(depth_of()).to_equal(0);
		expect(cursor_of(0)).to_equal(2);
	});

	it("a SELECTED over-long row scrolls, held across quiet ticks (task 10)", _ {
		// `ui/oled/list.cpp:156` -- the selected row's label runs through `scroll_text`, which
		// self-arms the panel's own gate (`ui/oled/scroll_text.cpp:68`) whenever the label overflows
		// its row; `tests/spec/ui/list_spec.cpp`'s "scrolls a selected label..." already pins that
		// mechanics directly. What was untested is the OTHER half: does `MenuScene::render_vertical`
		// actually reach it with a real, focused, over-long node? Every over-long row E2 task 12's
		// goldens ever recorded was unselected -- legacy truncates that case too, so nothing was
		// missing there -- and nothing exercised the selected one through the real scene.
		//
		// `nodes::feature_alt_delay_params`'s label is "Alternative Golden Knob Delay Params" (36
		// characters): comfortably past a 48-panel row, the same margin `list_spec.cpp`'s own
		// 41-character fixture uses. It sits at row 5 of `kRuntimeFeatureChildren`
		// (`menu/nodes/settings.h`: drum_randomizer, fine_tempo, quantize, catch_notes,
		// delete_unused_kit_rows, *alt_delay_params*, ...) -- `focused()` cannot confirm the cursor
		// landed on it (it names the node ON SCREEN, `st_.path[depth]`, which a turn never moves;
		// only a descent would), so this is deliberately self-checking instead: if a future reorder
		// ever puts a short label at row 5, the label will not overflow its row and `offset` will
		// stay 0, failing loudly rather than silently checking the wrong row.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = menu::context_for(*r.song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots); // the real tree, not this file's synthetic one
		menu_scene().open(nodes::runtime_features, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::runtime_features)});
		r.shell.tick(0);

		for (int i = 0; i < 5; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		expect(cursor_of(0)).to_equal(5);

		// Held across quiet ticks -- no input at all -- past `kScrollTime` (400 ms) plus one step
		// (`kScrollStepForwardMs`, 15 ms): the offset must have moved off zero on its own.
		expect(menu_scene().state().scroll[0].offset).to_equal(int16_t(0)); // not yet -- the pause
		r.shell.tick(460);
		r.shell.tick(475);
		expect(menu_scene().state().scroll[0].offset > 0).to_be_true();
	});
});

describe menu_scene_navigation("scenes::menu::MenuScene navigation", $ {
	it("a select-encoder detent moves the cursor without touching the model", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);
		expect(cursor_of(0)).to_equal(0);

		r.turn(1, 10);
		r.shell.tick(20);
		expect(cursor_of(0)).to_equal(1);
		expect(depth_of()).to_equal(0);

		r.turn(-1, 30);
		r.shell.tick(40);
		expect(cursor_of(0)).to_equal(0);
	});

	it("select on a submenu row descends, and back ascends", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);

		// Cursor is on the LPF page (row 0); a select-encoder press enters it.
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 10);
		r.shell.tick(20);
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &lpf_page).to_be_true();

		r.button(kBackX, kBackY, true, 30);
		r.shell.tick(40);
		expect(depth_of()).to_equal(0);
		expect(menu_scene().focused() == &settings_root).to_be_true();
	});

	it("back at the root removes the scene off the stack", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);
		expect(r.shell.stack().depth()).to_equal(size_t(1));

		// `handle()` directly, so the `Result` itself is the assertion (`sound_editor.cpp:456` ->
		// `:433` `goUpOneLevel()` -> `:678` `exitCompletely()`). `Remove`, not `Pop`: review round 1
		// (task 11) -- `Pop{.pop_to = kind}` still pops everything ABOVE `kind` too
		// (`scene_stack.cpp:63-95`), which is wrong for "close just me" the moment a `Notices`
		// overlay legitimately sits above this scene (see the test right after this one).
		DirectCtx d;
		const Result res = menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		expect(res.handled).to_be_true();
		expect(res.actions.size()).to_equal(size_t(1));
		expect(static_cast<int>(res.actions[0].kind)).to_equal(static_cast<int>(deluge::ui::shell::Action::Kind::Remove));
		expect(static_cast<int>(res.actions[0].pop_to)).to_equal(static_cast<int>(SceneKind::FirstApp));

		// And through the shell: the root's back is never claimed, so it reaches `handle()`
		// post-render and the stack empties.
		r.button(kBackX, kBackY, true, 10);
		r.shell.tick(20);
		expect(r.shell.stack().depth()).to_equal(size_t(0));
	});

	it("back at the root leaves a live overlay above it untouched", _ {
		// Review round 1 (task 11), I3: with the old `Pop{.pop_to = FirstApp}`, this scenario tore
		// the overlay down along with the menu -- `SceneStack::pop(kind)` removes that kind AND
		// everything above it. `Remove` must excise only the menu, leaving the overlay (wherever it
		// sits) exactly as it was.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);

		// A `Notices`-driven overlay legitimately pushed above the menu (the shell's own `Popup`
		// slot is wired regardless of the application's registry -- `Shell`'s constructor fills
		// `table_[Popup]` unconditionally).
		expect(r.shell.stack().push(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::Popup})).to_be_true();
		expect(r.shell.stack().depth()).to_equal(size_t(2));

		r.button(kBackX, kBackY, true, 10);
		r.shell.tick(20);
		expect(r.shell.stack().depth()).to_equal(size_t(1)); // only the menu is gone
		expect(r.shell.stack().top().kind() == deluge::ui::shell::SceneKind::Popup).to_be_true();
	});

	it("a shift-held shortcut pad replaces the scene at that node, and state_lines says so", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);
		expect(r.shell.state_lines().find("app16/" + std::to_string(menu::node_id(settings_root))) != std::string::npos)
		    .to_be_true();

		// Shift held, then the pad: `handle()` answers with Replace{FirstApp, node_id(target)}, the
		// router applies it, and `on_enter` re-targets on the way back in -- so `SceneId::arg`, and
		// therefore `state_lines()`, stay truthful about which node is open.
		r.button(kShiftX, kShiftY, true, 10);
		r.pad(kLpfFreqX, kLpfFreqY, 64, 20);
		r.shell.tick(30);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();
		expect(depth_of()).to_equal(0);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(r.shell.state_lines().find("app16/" + std::to_string(menu::node_id(nodes::lpf_freq)))
		       != std::string::npos)
		    .to_be_true();

		// Without shift the pad is not a shortcut (`sound_editor.cpp:1169`'s
		// `Buttons::isShiftButtonPressed()` gate), and nothing moves.
		r.button(kShiftX, kShiftY, false, 40);
		r.pad(kHpfFreqX, kHpfFreqY, 64, 50);
		r.shell.tick(60);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();
	});

	it("takes the second shortcut layer only on a second press of the focused item's own pad", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		// Legacy's `secondLayerShortcutsToggled` (`sound_editor.cpp:1364-1379`) -- and the reason
		// `shortcut_node`'s second argument is NOT "shift is held": shift is held throughout here.
		r.button(kShiftX, kShiftY, true, 0);
		r.pad(kEnv1RelX, kEnv1RelY, 64, 10);
		r.shell.tick(20);
		expect(menu_scene().focused() == &nodes::env1_release).to_be_true();

		// Same pad again: the second layer's `&env3ReleaseMenu`.
		r.pad(kEnv1RelX, kEnv1RelY, 64, 30);
		r.shell.tick(40);
		expect(menu_scene().focused() == &nodes::env3_release).to_be_true();

		// A different pad clears the toggle, so the pad after it is first-layer again.
		r.pad(kLpfFreqX, kLpfFreqY, 64, 50);
		r.shell.tick(60);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();
		r.pad(kEnv1RelX, kEnv1RelY, 64, 70);
		r.shell.tick(80);
		expect(menu_scene().focused() == &nodes::env1_release).to_be_true();
	});

	it("returns one Replace action naming the target's node id", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		DirectCtx shifted(/*shift=*/true);
		const Result res = menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Pad, .x = kLpfFreqX, .y = kLpfFreqY, .velocity = 64, .on = true},
		    shifted.ctx);
		expect(res.handled).to_be_true();
		expect(res.actions.size()).to_equal(size_t(1));
		expect(static_cast<int>(res.actions[0].kind)).to_equal(static_cast<int>(deluge::ui::shell::Action::Kind::Replace));
		expect(static_cast<int>(res.actions[0].id.kind)).to_equal(static_cast<int>(SceneKind::FirstApp));
		expect(static_cast<int>(res.actions[0].id.arg)).to_equal(static_cast<int>(menu::node_id(nodes::lpf_freq)));
		// `handle()` itself moves nothing: the router's Replace does, through `on_enter`.
		expect(menu_scene().focused() == &settings_root).to_be_true();
	});

	it("a MustSelectRange node opens the range picker, which then heads to the node it stood in for", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);
		(void)r.open(settings_root);
		r.shell.tick(0);

		// Walk to the range-dependent row (index 5) and press select.
		for (int i = 0; i < 5; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		expect(cursor_of(0)).to_equal(5);

		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		// E2 task 9: `enter_child` substitutes the picker for the item, which is legacy's own
		// `multiRangeMenu.menuItemHeadingTo = newItem; newItem = &multiRangeMenu;`
		// (`sound_editor.cpp:1857-1858`). So the *picker* is entered, not the leaf.
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &nodes::multi_range).to_be_true();
		expect(menu_scene().state().range_picker_pending).to_be_true();
		expect(menu_scene().state().pending_after_range == &transpose_leaf).to_be_true();

		// Both ranges are on screen, and the picker has selected one -- `setCurrentMultiRange`
		// (`sound_editor.cpp:1983-1986`), applied by `after_render` from `MenuState::range_choice`.
		// `MultiRange::beginSession` (`multi_range.cpp:46-50`): a `defaultRangeI` of -1 is invalid, so
		// the picker opens on `numRanges >> 1` -- range 1 of the two -- and `select_range` writes that
		// back, which is legacy's `soundEditor.currentSource->defaultRangeI = ...` (`:225`).
		expect(cursor_of(1)).to_equal(1);
		expect(static_cast<int>(sound->sources[0].defaultRangeI)).to_equal(1);

		// A select press on the picker chains to the node it stood in for -- `MultiRange::selectButtonPress`
		// returning `menuItemHeadingTo` (`multi_range.cpp:360-362`) -- and this time the permission
		// rule answers YES, because a range is now selected (`context.cpp`'s already-selected arm).
		//
		// Landing on the leaf is itself the proof that `select_range` reached the scene's own
		// `MenuContext`: with `ctx.range` still null the permission rule would answer `MustSelectRange`
		// a second time and the picker would be on screen again instead.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 120);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
		r.shell.tick(140);
		expect(depth_of()).to_equal(2);
		expect(menu_scene().focused() == &transpose_leaf).to_be_true();
		expect(menu_scene().state().range_picker_pending).to_be_false();
	});

	it("a range-dependent node on a ZERO-range source opens, because enter_child creates the range", _ {
		// E1's carried debt, end to end: `checkPermissionToBeginSessionForRangeSpecificParam`'s very
		// first statement is `source->getOrCreateFirstRange()` (`sound_editor.cpp:1993`), so a
		// sample item on a source with no ranges at all opens -- it does not bounce to a picker with
		// nothing in it. `tables_1_spec` drives `menu::ensure_first_range` directly; this drives the
		// caller, `MenuScene::enter_child`, which is where the ordering (create, *then* ask) lives.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		expect(sound->sources[0].ranges.empty()).to_be_true(); // the fixture starts with none
		(void)r.open(settings_root);
		r.shell.tick(0);

		for (int i = 0; i < 5; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		expect(cursor_of(0)).to_equal(5);

		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);

		// The leaf itself is on screen -- no picker in between -- and the range it needs now exists.
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &transpose_leaf).to_be_true();
		expect(menu_scene().state().range_picker_pending).to_be_false();
		expect(static_cast<int>(std::ssize(sound->sources[0].ranges))).to_equal(1);
		// `Source::getOrCreateFirstRange` gives it the top of the keyboard (`source.cpp:165`).
		expect(static_cast<int>(sound->sources[0].ranges.getElement(0)->topNote)).to_equal(32767);
	});

	it("a range-dependent node reached by SceneId::arg creates the range too, not only a descent", _ {
		// The same debt on the other route in. `MenuScene::enter_child` runs `ensure_first_range`
		// for a *descent*; a deep link does not descend, it re-roots -- `on_enter` resolves
		// `node_by_id(id.arg)` and hands it to `retarget`. Legacy makes no such distinction:
		// `SoundEditor::setup(clip, item, sourceIndex)` runs the named item's own
		// `checkPermissionToBeginSession` (`sound_editor.cpp:1845-1846`) whichever way the item was
		// asked for, and for a range-dependent one that is
		// `checkPermissionToBeginSessionForRangeSpecificParam`, whose first statement creates the
		// range (`:1993`).
		//
		// Two callers arrive this way and both were affected: site 8 of
		// `gui/ui/shell_menu_entry.cpp`'s table (`soundEditor.setup(getCurrentInstrumentClip(),
		// &menu_item::file0SelectorMenu, 0)`, `gui/views/instrument_clip_view.cpp:515`) and the
		// shortcut-pad jump, which re-enters through `Action::Kind::Replace` carrying the same
		// `arg`. Measured at level 3 before the fix: the ported file selector's browser refused to
		// open on a fresh synth clip -- `browser::sample_can` wants a non-null `AudioFileHolder`
		// (`scenes/browser/browser_scene.cpp:151-163`) and `ctx.range` was still null -- while the
		// toggle-OFF leg opened it. `tests/ui-scripts/app/menu.txt`'s `file-selector` snapshot is
		// that measurement; this is the same claim at fixture level.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		expect(sound->sources[0].ranges.empty()).to_be_true(); // the fixture starts with none

		// `Rig::open` ends in `shell.init(SceneId{FirstApp, node_id(root)})`, so opening the leaf
		// itself IS the deep link -- the same shape site 8 pushes.
		(void)r.open(transpose_leaf);
		r.shell.tick(0);

		expect(depth_of()).to_equal(0);
		expect(menu_scene().focused() == &transpose_leaf).to_be_true();
		expect(static_cast<int>(std::ssize(sound->sources[0].ranges))).to_equal(1);
		expect(static_cast<int>(sound->sources[0].ranges.getElement(0)->topNote)).to_equal(32767);
	});

	it("a deep link onto a MULTI-range source opens the picker, not the item", _ {
		// The other arm of the same gate, and the one F3 task 4's review caught missing. Legacy's
		// `checkPermissionToBeginSessionForRangeSpecificParam` answers `MUST_SELECT_RANGE` when the
		// source has more than one range and none is remembered (`sound_editor.cpp:2010-2014`), and
		// `SoundEditor::setup` then opens `multiRangeMenu` *instead of* the named item
		// (`:1855-1859`) -- for a named item exactly as for a navigated one, because `setup` runs
		// the check on `horizontal_menu_child` whichever way it was asked for.
		//
		// Paying only `ensure_first_range` and dropping this would be a partial port of a two-armed
		// gate: latent on a fresh source (0 ranges becomes 1, so `range_count == 1` and the item
		// just opens -- which is why the level-3 A/B cannot see it) and live the moment a user
		// presses LOAD on a multisampled oscillator.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);

		(void)r.open(transpose_leaf);
		r.shell.tick(0);

		// The picker is what is on screen, at depth 0 -- `retarget`, not `descend_to`, because a
		// deep link has no path to descend from -- and the item it stood in for is remembered.
		expect(depth_of()).to_equal(0);
		expect(menu_scene().focused() == &nodes::multi_range).to_be_true();
		expect(menu_scene().state().range_picker_pending).to_be_true();
		expect(menu_scene().state().pending_after_range == &transpose_leaf).to_be_true();
		// And the two ranges are still the source's own: nothing was allocated on top of them.
		expect(static_cast<int>(std::ssize(sound->sources[0].ranges))).to_equal(2);
	});

	it("a deep link to a node that is NOT range-dependent allocates nothing", _ {
		// The other half of the guard, so the arm above cannot be satisfied by allocating for every
		// deep link. Legacy runs the range check only for the items whose
		// `checkPermissionToBeginSession` routes into it -- `MenuItem::isRangeDependent()`
		// (`gui/menu_item/menu_item.h:202`), which is `Node::range_dependent` -- and an ordinary
		// param takes `MenuItem::checkPermissionToBeginSession` (`menu_item.cpp:27-31`), which
		// never looks at a range at all.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		expect(sound->sources[0].ranges.empty()).to_be_true();

		(void)r.open(toggle_leaf);
		r.shell.tick(0);

		expect(menu_scene().focused() == &toggle_leaf).to_be_true();
		expect(sound->sources[0].ranges.empty()).to_be_true();
	});

	it("save_state and restore_state round-trip a depth-3 path", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);

		// root -> FILTERS (row 6) -> LPF (row 0) -> PAN (row 0): three levels down.
		for (int i = 0; i < 6; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		expect(cursor_of(0)).to_equal(6);
		r.enter(100);
		r.shell.tick(110);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 120);
		r.enter(130);
		r.shell.tick(140);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 150);
		r.enter(160);
		r.shell.tick(170);
		expect(depth_of()).to_equal(3);
		expect(menu_scene().focused() == &deep_leaf).to_be_true();

		deluge::ui::shell::SceneState saved{};
		menu_scene().save_state(saved);

		menu_scene().open(settings_root, menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false));
		expect(depth_of()).to_equal(0);

		menu_scene().restore_state(saved);
		expect(depth_of()).to_equal(3);
		expect(menu_scene().focused() == &deep_leaf).to_be_true();
		// Every level on the way down is seeded with the row it was left on, not row 0.
		expect(cursor_of(0)).to_equal(6);
		expect(cursor_of(1)).to_equal(0);
		expect(cursor_of(2)).to_equal(0);
	});

	it("a leaf's select press opens the menu its select_chain names, instead of ascending", _ {
		// The port of `MenuItem::selectButtonPress` returning a menu (`gui/ui/sound_editor.cpp:395`
		// -> `enterSubmenu`). `chain_source_leaf`'s hook chains only on an odd value, so this drives
		// both answers through the real shell and asserts each.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);

		// Descend to the chaining leaf (row 8, the last child).
		for (int i = 0; i < 8; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		expect(cursor_of(0)).to_equal(8);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		expect(menu_scene().focused() == &chain_source_leaf).to_be_true();
		expect(depth_of()).to_equal(1);

		// Odd value -> the hook names `chain_target_leaf`, so the press descends rather than
		// ascending.
		g_int = 3;
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 240);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 250);
		r.shell.tick(260);
		expect(menu_scene().focused() == &chain_target_leaf).to_be_true();
		expect(depth_of()).to_equal(2);

		// And the chained page chains on again -- two hops, the shape `arp_preset` ->
		// `arp_octave_mode_to_note_mode` -> `arp_note_mode_from_octave_mode` has.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 270);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 280);
		r.shell.tick(290);
		expect(menu_scene().focused() == &chain_target_hop2).to_be_true();
		expect(depth_of()).to_equal(3);

		// `chain_target_hop2` has no chain, so its press ascends -- back to the page that chained
		// to it, not to the root.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 300);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 310);
		r.shell.tick(320);
		expect(menu_scene().focused() == &chain_target_leaf).to_be_true();
		expect(depth_of()).to_equal(2);
	});

	it("a leaf whose select_chain answers null still ascends", _ {
		// The other branch of the same arm, and the one that regresses silently if the chain consult
		// is written as an unconditional descend: an even value means "no menu", i.e. legacy's
		// `return nullptr` = `goUpOneLevel()`.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);
		for (int i = 0; i < 8; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		expect(menu_scene().focused() == &chain_source_leaf).to_be_true();

		g_int = 4; // even
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 120);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
		r.shell.tick(140);
		expect(depth_of()).to_equal(0);
		expect(menu_scene().focused() == &settings_root).to_be_true();
	});

	it("gives a chain-only page an id, so on_enter can restore one", _ {
		// `assign_node_ids` follows `chain_targets`: without that, `chain_target_leaf` -- which no
		// `children` span names -- would have id 0 and `SceneId::arg` could never address it.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);

		const uint16_t id = menu::node_id(chain_target_leaf);
		expect(id != uint16_t(0)).to_be_true();
		expect(menu::node_by_id(id) == &chain_target_leaf).to_be_true();
		expect(menu::node_id(chain_target_hop2) != uint16_t(0)).to_be_true();

		expect(menu_scene().on_enter(SceneId{SceneKind::FirstApp, id})).to_be_true();
		expect(menu_scene().focused() == &chain_target_leaf).to_be_true();
	});
});

describe menu_scene_editing("scenes::menu::MenuScene leaf editing", $ {
	it("a select-encoder detent on an Int leaf commits through bindings::write", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_int = 3;
		r.shell.tick(0);

		// Row 1 is the Int leaf; step onto it and enter it.
		r.turn(1, 10);
		r.shell.tick(20);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 30);
		r.shell.tick(40);
		expect(menu_scene().focused() == &int_leaf).to_be_true();
		expect(menu::bindings::read(int_leaf, ctx)).to_equal(3);

		// `int_value` edits on the select encoder now, not the gold knobs
		// (`ui/oled/values.cpp`'s `select_delta`) -- legacy's `Integer::selectEncoderAction`.
		r.turn(2, 50);
		r.shell.tick(60);
		expect(g_int).to_equal(5);
		expect(menu::bindings::read(int_leaf, ctx)).to_equal(5);
	});

	it("a Toggle leaf's select press goes up without flipping the value", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_toggle = false;
		r.shell.tick(0);

		for (int i = 0; i < 4; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		expect(cursor_of(0)).to_equal(4);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		expect(menu_scene().focused() == &toggle_leaf).to_be_true();

		// `values.h:169`: a select press reports `entered` and leaves `changed` false.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 120);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 130);
		r.shell.tick(140);
		expect(depth_of()).to_equal(0);
		expect(menu::bindings::read(toggle_leaf, ctx)).to_equal(0);
	});

	it("a turn on an Enum leaf moves the option and commits it", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_enum = 1;
		r.shell.tick(0);

		for (int i = 0; i < 3; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		expect(menu_scene().focused() == &enum_leaf).to_be_true();

		r.turn(1, 120);
		r.shell.tick(130);
		expect(menu::bindings::read(enum_leaf, ctx)).to_equal(2);
	});

	it("a Decimal leaf edits at its own places and commits", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_decimal = 1234;
		r.shell.tick(0);

		for (int i = 0; i < 2; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		expect(menu_scene().focused() == &decimal_leaf).to_be_true();

		// `decimal_value`'s select-detent amount is 10^edit_pos, and `edit_pos` starts at 0; it
		// reads the select encoder now, not the gold knobs (`ui/oled/values.cpp`'s `select_delta`).
		r.turn(1, 120);
		r.shell.tick(130);
		expect(menu::bindings::read(decimal_leaf, ctx)).to_equal(1235);
	});

	it("a SyncLevel leaf reads OFF at zero and edits through the select encoder, not the gold knobs", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		g_sync = 0;
		r.shell.tick(0);

		// Row 7 (last) is the SyncLevel leaf; step onto it and enter it.
		for (int i = 0; i < 7; i++) {
			r.turn(1, static_cast<uint32_t>(10 + i * 10));
			r.shell.tick(static_cast<uint32_t>(15 + i * 10));
		}
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.shell.tick(110);
		expect(menu_scene().focused() == &sync_leaf).to_be_true();
		expect(menu::bindings::read(sync_leaf, ctx)).to_equal(0);

		// `SyncLevel` derives from `Enumeration` (`sync_level.h:27`), not `Integer`, so the SELECT
		// ENCODER moves the value -- unlike `int_leaf`/`decimal_leaf` above, which read the gold
		// knobs. A gold-knob turn here must do nothing.
		r.gold(kGoldUpper, 2, 120);
		r.shell.tick(130);
		expect(menu::bindings::read(sync_leaf, ctx)).to_equal(0);

		r.turn(1, 140);
		r.shell.tick(150);
		expect(menu::bindings::read(sync_leaf, ctx)).to_equal(1);

		// Clamped at the top of `kSyncLevelRange`, like `Enumeration::selectEncoderAction`'s
		// non-wrapping clamp (`enumeration.cpp:19-31`) -- a turn well past the top lands on the
		// last option, not wrapped back to 0.
		r.turn(static_cast<int32_t>(NUM_SYNC_VALUES) + 5, 160);
		r.shell.tick(170);
		expect(menu::bindings::read(sync_leaf, ctx)).to_equal(NUM_SYNC_VALUES - 1);
	});

	it("the horizontal page edits the focused column through the select encoder", _ {
		// `HorizontalMenu::selectEncoderAction` (`horizontal_menu.cpp:309-326`): the select turn
		// edits the focused column and never moves focus. Re-derived from the gold-knob version
		// this replaces (E-note-family task 2): the gold knobs no longer touch a horizontal page at
		// all -- they fall through to `services::ModKnobs` (spec §3.2's fourth bullet).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open(settings_root);
		r.shell.tick(0);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 10);
		r.shell.tick(20);
		expect(menu_scene().focused() == &lpf_page).to_be_true();

		const int32_t before = menu::bindings::read(lpf_freq, ctx);
		r.turn(-1, 30);
		r.shell.tick(40);
		expect(menu::bindings::read(lpf_freq, ctx)).to_equal(before - 1);
		expect(deluge::notify::dirty().has(deluge::notify::Dirty::ParamAutomation)).to_be_true();
	});
});

describe menu_scene_claims("scenes::menu::MenuScene input claims", $ {
	it("claims the turn and the select but never the back at the root", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		deluge::ui::test::Rig48 rig;
		rig.begin_tick(0);
		rig.input.back_pressed = true;
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);
		expect(rig.ctx.claims.turn).to_be_true();
		expect(rig.ctx.claims.select).to_be_true();
		expect(rig.ctx.claims.back).to_be_false();
		expect(rig.ctx.claims.double_claim).to_be_false();
	});

	it("claims the back once it has somewhere to go up to", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 10);
		r.shell.tick(20);
		expect(depth_of()).to_equal(1);

		deluge::ui::test::Rig48 rig;
		rig.begin_tick(0);
		menu_scene().render_oled(SurfaceRole::Main, rig.frame);
		expect(rig.ctx.claims.back).to_be_true();
		expect(rig.ctx.claims.double_claim).to_be_false();
	});

	it("renders nothing for a role this board does not have", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		deluge::ui::test::Rig48 rig;
		rig.begin_tick(0);
		menu_scene().render_oled(SurfaceRole::Aux, rig.frame);
		expect(rig.pbm()).to_equal(deluge::ui::test::Rig48{}.pbm());
		expect(rig.ctx.claims.any()).to_be_false();
	});

	it("a full shell tick never double-claims an input", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.shell.tick(0);
		r.turn(1, 10);
		r.shell.tick(20);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 30);
		r.shell.tick(40);
		// `InputClaims::double_claim` is the shell's own diagnostic for "one input, two frames";
		// both render passes share `Shell::lowering().ctx()`, so this is the whole tick's answer.
		expect(r.double_claimed()).to_be_false();
		expect(cursor_of(0)).to_equal(1);
		expect(depth_of()).to_equal(1); // row 1 is the Int leaf

		// And through every other screen shape the interpreter has, one tick at a time.
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 50);
		r.button(kBackX, kBackY, true, 60);
		r.shell.tick(70);
		expect(depth_of()).to_equal(0);
		expect(r.double_claimed()).to_be_false();

		r.turn(-1, 80);
		r.shell.tick(90);
		expect(cursor_of(0)).to_equal(0);
		expect(r.double_claimed()).to_be_false();

		r.enter(100); // into the horizontal LPF page
		r.shell.tick(110);
		expect(depth_of()).to_equal(1);
		expect(r.double_claimed()).to_be_false();

		// The gold knob falls through to `services::ModKnobs` on a horizontal page now (no menu
		// effect, so `MenuScene` claims nothing for it); the select turn is what edits the focused
		// column (`HorizontalMenu::selectEncoderAction`, `horizontal_menu.cpp:309-326`), and is the
		// only one of the two `MenuScene` claims here. Landing both in the same tick still cannot
		// double-claim: `kGoldUpper` and `kSelectEncoder` are separate encoder indices, so nothing
		// in this Rig ever asks one input to be claimed twice.
		r.gold(kGoldUpper, 1, 120);
		r.turn(1, 130);
		r.shell.tick(140);
		expect(r.double_claimed()).to_be_false();

		r.button(kBackX, kBackY, false, 150);
		r.button(kBackX, kBackY, true, 160);
		r.shell.tick(170);
		expect(depth_of()).to_equal(0);
		expect(r.double_claimed()).to_be_false();
	});
});

describe menu_scene_grid("scenes::menu::MenuScene grid overlay", $ {
	it("paints the focused shortcut pad and nothing else", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		// Jump to `lpfFreqMenu`'s real cell, (8, 7) of `paramShortcutsForSounds`. Through the shell,
		// because the jump is a `Replace` the router applies -- `handle()` alone moves nothing.
		r.button(kShiftX, kShiftY, true, 0);
		r.pad(kLpfFreqX, kLpfFreqY, 64, 1);
		r.shell.tick(2);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();

		// The cycle is anchored at the first render of the newly focused node (legacy's
		// `shortcutBlinkCounter = 0`), so phases are measured from there, not from t = 0.
		const uint32_t anchor = menu_scene().state().blink_anchor_ms;
		deluge::ui::test::GridRig grid;
		grid.begin_tick(anchor + menu::kSourceBlinkMs); // the param phase of the cycle: lit
		menu_scene().render_grid(grid.frame);

		int painted = 0;
		for (int32_t y = 0; y < grid.grid.dims().rows; y++) {
			for (int32_t x = 0; x < grid.grid.dims().cols(); x++) {
				const RGB& c = grid.grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				if (c.r != 0 || c.g != 0 || c.b != 0) {
					painted++;
					expect(x).to_equal(int32_t{kLpfFreqX});
					expect(y).to_equal(int32_t{kLpfFreqY});
				}
			}
		}
		expect(painted).to_equal(1);
		expect(grid.ctx.claims.any()).to_be_false();
	});

	it("runs legacy's two-phase 200 ms cycle, not a symmetric 180 ms one", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.button(kShiftX, kShiftY, true, 0);
		r.pad(kLpfFreqX, kLpfFreqY, 64, 1);
		r.shell.tick(2);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();

		// `SoundEditor::blinkShortcut` (`sound_editor.cpp:1012-1038`) alternates on one counter: the
		// source phase re-arms at 20 ms and the param phase at 180 ms, so the cycle is 200 ms and
		// the source slice is the short leading one. Rendered directly, at the phase boundaries, so
		// this asserts the *timings* rather than "it blinks".
		//
		// Measured from the cycle's anchor, which `setupShortcutBlink` zeroes on every focus change
		// (`sound_editor.cpp:1002`) and `render_grid` re-sets whenever the focused node changes.
		const uint32_t anchor = menu_scene().state().blink_anchor_ms;
		const auto lit_at = [&](uint32_t offset_ms) {
			deluge::ui::test::GridRig grid;
			grid.begin_tick(anchor + offset_ms);
			menu_scene().render_grid(grid.frame);
			const RGB& c = grid.grid.at(kLpfFreqX, kLpfFreqY);
			return c.r != 0 || c.g != 0 || c.b != 0;
		};
		expect(uint32_t{menu::kBlinkCycleMs}).to_equal(uint32_t{200});
		expect(lit_at(0)).to_be_false();   // source phase [0, 20)
		expect(lit_at(19)).to_be_false();
		expect(lit_at(20)).to_be_true();   // param phase [20, 200)
		expect(lit_at(100)).to_be_true();
		expect(lit_at(199)).to_be_true();
		expect(lit_at(200)).to_be_false(); // next cycle's source phase
		expect(lit_at(219)).to_be_false();
		expect(lit_at(220)).to_be_true();
		// Not a symmetric 180 ms square wave: that would have been dark here and lit at 200.
		expect(lit_at(180)).to_be_true();
	});

	it("restarts the blink cycle when the focused node changes", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.button(kShiftX, kShiftY, true, 0);
		r.pad(kLpfFreqX, kLpfFreqY, 64, 1);
		r.shell.tick(2);
		const uint32_t first = menu_scene().state().blink_anchor_ms;

		// Rendering the same node again leaves the cycle where it was ...
		deluge::ui::test::GridRig same;
		same.begin_tick(first + 500);
		menu_scene().render_grid(same.frame);
		expect(menu_scene().state().blink_anchor_ms).to_equal(first);

		// ... but jumping to another pad re-anchors, exactly as `setupShortcutBlink`'s
		// `shortcutBlinkCounter = 0` does (`sound_editor.cpp:1002`). Without that the port would
		// free-run and a freshly focused pad could light mid-cycle.
		r.pad(kHpfFreqX, kHpfFreqY, 64, 600);
		r.shell.tick(601);
		expect(menu_scene().focused() == &nodes::hpf_freq).to_be_true();
		const uint32_t second = menu_scene().state().blink_anchor_ms;
		expect(second != first).to_be_true();

		// And the new cycle starts in the source phase, i.e. dark, at its own anchor.
		deluge::ui::test::GridRig fresh;
		fresh.begin_tick(second);
		menu_scene().render_grid(fresh.frame);
		const RGB& dark = fresh.grid.at(kHpfFreqX, kHpfFreqY);
		expect(dark.r == 0 && dark.g == 0 && dark.b == 0).to_be_true();
	});

	it("arms the grid gate so the blink flips at the next phase boundary", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		// Jump to `lpfFreqMenu` so its shortcut cell (8, 7) is the focused pad.
		r.button(kShiftX, kShiftY, true, 0);
		r.pad(kLpfFreqX, kLpfFreqY, 64, 1);
		r.shell.tick(2);
		r.button(kShiftX, kShiftY, false, 3);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();

		// Phases run from the cycle's anchor (the first render of the newly focused node).
		const uint32_t anchor = menu_scene().state().blink_anchor_ms;

		// Inside the param phase: lit.
		r.shell.tick(anchor + 30);
		const RGB lit = r.pad_colour(kLpfFreqX, kLpfFreqY);
		expect(lit.r != 0 || lit.g != 0 || lit.b != 0).to_be_true();

		// The scene armed the grid gate for the boundary, so this tick renders with no input at all
		// -- which is the whole point: `next_timer()` alone would wake the shell without making
		// `RepaintGate::needs_frame` say yes. One whole cycle on is the next source phase.
		r.shell.tick(anchor + menu::kBlinkCycleMs);
		const RGB dark = r.pad_colour(kLpfFreqX, kLpfFreqY);
		expect(dark.r == 0 && dark.g == 0 && dark.b == 0).to_be_true();

		// And back again 20 ms later, when the param phase resumes -- not 180.
		r.shell.tick(anchor + menu::kBlinkCycleMs + menu::kSourceBlinkMs);
		const RGB relit = r.pad_colour(kLpfFreqX, kLpfFreqY);
		expect(relit.r == lit.r && relit.g == lit.g && relit.b == lit.b).to_be_true();
	});

	it("the source-pad blink cadence never blits the OLED panels (E3 task 10 fix round 1)", _ {
		// The regression the fold's first cut introduced: `MenuScene::next_timer()` returns
		// `kSourceBlinkMs` whenever the focused node has source shortcut pads -- `lpf_freq` does,
		// which is exactly the previous test's fixture -- and that timer is purely a GRID concern
		// (the submenu's OLED content never depends on it). Before `timer_repaints()` could name a
		// surface, step 8b armed BOTH kinds of surface from every scene timer; the grid's own
		// `flush` diffs against `last_sent` and blits nothing when a repaint changed no pixel, but
		// the OLED panels have no such diff -- `Shell::flush` blits one unconditionally once it has
		// rendered. So sitting on `lpf_freq` blitted both OLED panels, unconditionally, forever, at
		// 50 Hz -- with no pixel ever changing, which is why no golden or pixel comparison could see
		// it.
		//
		// Two other, unrelated reasons an OLED blit can legitimately happen here, both excluded by
		// construction rather than by luck:
		//   * `decimal_value` (`lpf_freq` is a Decimal leaf) owns its own edit-cursor blink
		//     (`kFlashTime` = 110 ms half-period, `ui/oled/values.cpp`), armed once from the tick
		//     that first draws it and not again until that half-period elapses -- settled below,
		//     before the measured window opens.
		//   * `MenuScene::render_grid`'s OWN tighter blink arm (`ms_to_phase_change`, the previous
		//     test's own subject) shares `FrameInput::gate()` with the OLED widgets -- `OledFrame`
		//     and `GridFrame` both resolve `gate()` to the same `FrameContext::gate`
		//     (`ui/core/frame_context.h`) -- so it also fires an OLED render at every PHASE
		//     boundary (`kSourceBlinkMs`/`kBlinkCycleMs` apart), independent of `timer_repaints()`
		//     entirely: that hook governs `Shell::tick` step 8b alone, not a scene's own `render_*`
		//     arms. This is real, pre-existing, and outside this fix's scope -- but it fires only
		//     AT a boundary, not on every tick inside a phase, so the window below stays strictly
		//     inside one phase (the long 180 ms "shortcut" phase) to avoid crossing it.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		r.button(kShiftX, kShiftY, true, 0);
		r.pad(kLpfFreqX, kLpfFreqY, 64, 1);
		r.shell.tick(2);
		r.button(kShiftX, kShiftY, false, 3);
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();

		// Settle past both confounds: the "entered leaf draws next tick" one-shot repaint, the
		// decimal cursor's own first arm, AND the source -> shortcut phase boundary at
		// `kSourceBlinkMs` -- all landing inside the shortcut phase before the measured window
		// opens.
		const uint32_t anchor = menu_scene().state().blink_anchor_ms;
		r.shell.tick(anchor + menu::kSourceBlinkMs + 5);
		const size_t blits_before = r.fake.blits.size();

		// Eight cycles of the (fixed) source-pad cadence, no input at all, entirely inside the
		// shortcut phase (`kSourceBlinkMs` = 20 to `kBlinkCycleMs` = 200, relative to `anchor`) --
		// no phase boundary crossed, so `ms_to_phase_change`'s own arm stays quiet throughout and
		// the only remaining candidate is the bug this fix removes. (Not asserting the grid kept
		// moving here: the pad it paints is genuinely static for the length of one phase, same as
		// the OLED content -- that half of the mechanism is what the shell-level `tick_spec.cpp`
		// example proves instead, with a synthetic scene whose grid content really does change on
		// the same cadence.)
		for (int i = 1; i <= 8; i++) {
			r.shell.tick(anchor + menu::kSourceBlinkMs + 5 + static_cast<uint32_t>(i) * menu::kSourceBlinkMs);
		}

		expect(r.fake.blits.size()).to_equal(blits_before); // zero OLED blits, the whole time
	});

	it("declares button LEDs only, with the back LED blinking", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);

		deluge::ui::LedFrame frame;
		menu_scene().leds(frame);
		bool saw_back_blink = false;
		for (const auto& [id, state] : frame.entries()) {
			if (id == static_cast<deluge::ui::LedId>(ButtonId::Back)) {
				saw_back_blink = state.kind == deluge::ui::LedState::Kind::Blink;
			}
		}
		expect(saw_back_blink).to_be_true();
		expect(frame.indicator_or_null(deluge::ui::IndicatorId::Lower) == nullptr).to_be_true();
		expect(frame.indicator_or_null(deluge::ui::IndicatorId::Upper) == nullptr).to_be_true();
	});
});

describe menu_scene_columns("scenes::menu::MenuScene horizontal page", $ {
	/// @brief Open the LPF page and focus column \p column (0 = FREQ .. 3 = MORPH).
	///
	/// Shared by the formatting cases below and by the goldens, so both start from exactly the
	/// same state. Re-derived for E-note-family task 2: legacy moves horizontal focus by
	/// instrument button, never by turn (`HorizontalMenu::selectEncoderAction`,
	/// `horizontal_menu.cpp:309-326`, edits and never moves focus), and SYNTH/KIT/MIDI/CV name a
	/// page's four columns directly (`select_map`, `:97`) rather than being walked to one at a
	/// time -- so a single press reaches any column, where the old version turned once per column.
	static const auto open_lpf = [](Rig& r, int32_t column, uint32_t base_ms) {
		r.shell.tick(0);
		r.enter(base_ms);
		r.shell.tick(base_ms + 10);

		if (column > 0) {
			// The press lands 200 ms past `base_ms`, clearing the six buttons' shared 140 ms
			// debounce off `MenuState::nav_press_ms`'s zero default (`horizontal_menu.cpp:77-82`) --
			// this is the first of them pressed since `open()` reset it.
			constexpr uint8_t kX[] = {kSynthX, kKitX, kMidiX, kCvX};
			constexpr uint8_t kY[] = {kSynthY, kKitY, kMidiY, kCvY};
			const uint32_t press = base_ms + 200;
			r.button(kX[column], kY[column], true, press);
			r.shell.tick(press + 10);
			r.button(kX[column], kY[column], false, press + 20);
			r.shell.tick(press + 30);
		}

		// One more tick with no input: a render pass draws the screen it was *entered* with (or
		// last focused), and `after_render` moves the path afterwards, so a freshly focused screen
		// is only drawn on the tick after the one that focused it -- which the scene asks for with
		// `RepaintAll`. Column 0 needs no button at all: `descend_to` already resets
		// `columns[depth].focused` to 0.
		r.shell.tick(base_ms + 260);
	};

	/// @brief The last notice raised, whole -- the Decimal example reads a `Notification`'s value half.
	static const auto last_raised_notice = []() -> deluge::notify::Notice {
		deluge::notify::Notice n;
		deluge::notify::Notice last{};
		while (deluge::notify::notices().pop(n)) {
			last = n;
		}
		return last;
	};
	/// @brief The text of the notice the last tick raised, or "" if none.
	static const auto last_notice = []() -> std::string {
		deluge::notify::Notice n;
		std::string text;
		while (deluge::notify::notices().pop(n)) {
			text = n.text;
		}
		return text;
	};

	it("focuses the first column of the first page", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		open_lpf(r, 0, 10);

		expect(menu_scene().focused() == &lpf_page).to_be_true();
		expect(depth_of()).to_equal(1);
		expect(static_cast<int>(menu_scene().state().columns[1].focused)).to_equal(0);
		expect(static_cast<int>(menu_scene().state().columns[1].page)).to_equal(0);
	});

	it("focuses a column directly by its instrument button", _ {
		// `HorizontalMenu::buttonAction`'s `select_map` (`horizontal_menu.cpp:97`): SYNTH/KIT/MIDI/CV
		// name columns 0-3 of the visible page directly, in one press. Replaces a test of "one
		// column per detent" -- that walk is gone; the select turn edits the focused column now and
		// never moves it (`selectEncoderAction`, `:309-326`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		open_lpf(r, 3, 10); // CV -> column 3, MORPH

		expect(static_cast<int>(menu_scene().state().columns[1].focused)).to_equal(3);
		expect(static_cast<int>(menu_scene().state().columns[1].page)).to_equal(0); // four columns, one page
	});

	it("formats a Decimal column with the point always placed", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(settings_root);
		open_lpf(r, 3, 10); // MORPH, two decimal places, CV's column

		// Legacy pads to `places + 1` digits before placing the point
		// (`Decimal::drawPixelsForOled`, `gui/menu_item/decimal.cpp:129-153`), so a value smaller
		// than one whole unit still reads as `0.xx` rather than losing the point entirely.
		struct Case {
			int32_t value;
			const char* text;
		};
		constexpr Case kCases[] = {{5, "0.05"}, {0, "0.00"}, {-5, "-0.05"}, {1234, "12.34"}};
		uint32_t at = 600;
		for (const Case& c : kCases) {
			g_morph = c.value;
			(void)last_notice(); // drain
			// A select press on a value column no longer raises this notification -- it follows the
			// column's `select_chain`, or ascends, and stays silent either way (spec E-note-family
			// §3.2's third bullet). The already-focused CV press does instead:
			// `handleItemAction` (`horizontal_menu.cpp:663-679`) runs the column's own press (a
			// no-op here -- only `Toggle` overrides `selectButtonPress`) and notifies, without
			// touching the value, which is exactly what this case wants formatted.
			r.button(kCvX, kCvY, true, at);
			r.shell.tick(at + 10);
			expect(depth_of()).to_equal(1);
			// Read the notice off the same tick that raised it: `Shell::tick` drains
			// `notify::notices()` into its own `NoticesController` at the *start* of the next tick
			// (`shell.cpp:327`), so a second tick here (the button's release) would consume it
			// before this can.
			const deluge::notify::Notice notice = last_raised_notice();
			expect(notice.kind == deluge::notify::Notice::Kind::Notification).to_be_true();
			expect(std::string(notice.notification_value())).to_equal(std::string(c.text));
			r.button(kCvX, kCvY, false, at + 20);
			r.shell.tick(at + 30);
			at += 200; // past the six buttons' shared 140 ms debounce
		}
	});

	it("draws the four-column page the same on both shipped panel geometries", _ {
		select_language();
		deluge::notify::dirty().drain();
		g_morph = 1234;
		g_lpf_mode = 1;
		{
			Rig r;
			(void)r.open(settings_root);
			open_lpf(r, 0, 10);
			expect(depth_of()).to_equal(1);
			expect(menu_scene().focused() == &lpf_page).to_be_true();
			expect(static_cast<int>(menu_scene().state().columns[1].focused)).to_equal(0);
			expect(static_cast<int>(menu_scene().state().columns[1].page)).to_equal(0);
			expect_golden(self, "../ui/menu/menu_lpf_columns_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			(void)r64.open(settings_root);
			open_lpf(r64, 0, 10);
			expect(depth_of()).to_equal(1);
			expect(menu_scene().focused() == &lpf_page).to_be_true();
			expect(static_cast<int>(menu_scene().state().columns[1].focused)).to_equal(0);
			expect(static_cast<int>(menu_scene().state().columns[1].page)).to_equal(0);
			expect_golden(self, "../ui/menu/menu_lpf_columns_64.pbm", r64.fake.pbm(0));
		}
	});
});

describe menu_scene_goldens("scenes::menu::MenuScene pixels", $ {
	it("draws the settings root the same on both shipped panel geometries", _ {
		select_language();
		deluge::notify::dirty().drain();
		{
			Rig r;
			(void)r.open(settings_root);
			r.shell.tick(0);
			expect(depth_of()).to_equal(0);
			expect(cursor_of(0)).to_equal(0);
			expect_golden(self, "../ui/menu/menu_settings_root_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			(void)r64.open(settings_root);
			r64.shell.tick(0);
			expect(depth_of()).to_equal(0);
			expect(cursor_of(0)).to_equal(0);
			expect_golden(self, "../ui/menu/menu_settings_root_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws a SyncLevel leaf's note-length string centred, on both shipped panel geometries", _ {
		select_language();
		deluge::notify::dirty().drain();
		g_sync = 7; // a real note length, not the zero/OFF arm -- the text this task exists to draw
		{
			Rig r;
			(void)r.open(settings_root);
			r.shell.tick(0);
			for (int i = 0; i < 7; i++) {
				r.turn(1, static_cast<uint32_t>(10 + i * 10));
				r.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.shell.tick(110);
			expect(menu_scene().focused() == &sync_leaf).to_be_true();
			// `after_render` moves the path only *after* the render that entered it, so the panel
			// still shows the old (list) screen this same tick -- the entered leaf is only drawn on
			// the next one (`descend_to`'s `st_.path[st_.depth] = &child`, `menu_scene.cpp:1458`;
			// `open_lpf` above needs the identical extra tick for the same reason).
			r.shell.tick(120);
			expect_golden(self, "../ui/menu/menu_sync_level_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			(void)r64.open(settings_root);
			r64.shell.tick(0);
			for (int i = 0; i < 7; i++) {
				r64.turn(1, static_cast<uint32_t>(10 + i * 10));
				r64.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r64.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r64.shell.tick(110);
			expect(menu_scene().focused() == &sync_leaf).to_be_true();
			r64.shell.tick(120);
			expect_golden(self, "../ui/menu/menu_sync_level_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws a SyncLevel leaf's OFF text centred, on both shipped panel geometries -- the zero "
	   "branch the non-zero golden above does not exercise", _ {
		select_language();
		deluge::notify::dirty().drain();
		g_sync = 0; // the zero/OFF arm: a different l10n string and a different buffer path entirely
		{
			Rig r;
			(void)r.open(settings_root);
			r.shell.tick(0);
			for (int i = 0; i < 7; i++) {
				r.turn(1, static_cast<uint32_t>(10 + i * 10));
				r.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r.shell.tick(110);
			expect(menu_scene().focused() == &sync_leaf).to_be_true();
			r.shell.tick(120); // same "entered leaf draws next tick" rule as the golden above
			expect_golden(self, "../ui/menu/menu_sync_level_off_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			(void)r64.open(settings_root);
			r64.shell.tick(0);
			for (int i = 0; i < 7; i++) {
				r64.turn(1, static_cast<uint32_t>(10 + i * 10));
				r64.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r64.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
			r64.shell.tick(110);
			expect(menu_scene().focused() == &sync_leaf).to_be_true();
			r64.shell.tick(120);
			expect_golden(self, "../ui/menu/menu_sync_level_off_64.pbm", r64.fake.pbm(0));
		}
	});
});
// clang-format on

/// Eight `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead -- as `context_spec.cpp` does.
int menu_scene_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_scene_composition, menu_scene_navigation, menu_scene_editing, menu_scene_claims,
	                          menu_scene_grid, menu_scene_columns, menu_scene_goldens, menu_scene_info_and_custom)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
