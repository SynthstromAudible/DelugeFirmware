/// @file
/// @brief `MenuScene` horizontal-page controls (E-note-family task 2): the select turn edits the
///        focused column, the instrument buttons focus and page-switch, and a select press on a
///        value column follows its chain or ascends -- all driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, the same rig `menu_scene_spec.cpp` uses.
///
/// The tree under test is declared here rather than taken from `src/scenes/menu/nodes/`, for the
/// same reason `menu_scene_spec.cpp`'s is: this task's subject is the interpreter, not the data,
/// and the shipped `nodes::lpf` (four value columns, no submenu column, no range-dependent one) is
/// not shaped to exercise every arm this file has to -- `is_submenu`'s no-op, `select_chain`'s
/// both branches, and `Permission::MustSelectRange` reached by a button, all need a column the
/// real table does not have. `nodes::lpf` is still what the level-2 acceptance script
/// (`tests/ui-scripts/shell/menu_submenu_horizontal.txt`) opens; this file's local tree is the
/// toolkit-level counterpart `columns_spec.cpp` already established the pattern for.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/detail.h" // with_slots
#include "scenes/menu/nodes/nodes.h"  // the real `nodes::randomizer`/`randomizer_lock`
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

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
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The tree under test: a seven-column, two-page horizontal page.
//
// Page 1 (four one-slot columns, buttons SYNTH/KIT/MIDI/CV in order):
//   0 SYNTH  ENUM_COL    Kind::Enum, no chain      -- turn-edits-value, and the "ascends" half of
//                                                     the select-press test
//   1 KIT    TOGGLE_COL  Kind::Toggle               -- the already-focused-press/notify example,
//                                                     and the select-press flip-and-stay example
//   2 MIDI   SUBMENU_COL Kind::Submenu              -- the turn-does-nothing example, and the
//                                                     no-notification-on-focus example
//   3 CV     CHAIN_COL   Kind::Int, with select_chain -- the "enters the chain target" half
// Page 2 (three one-slot columns, SYNTH/KIT/MIDI again once paged across):
//   4 SYNTH  RANGE_COL   Kind::Int, range_dependent -- the MustSelectRange button-focus example
//   5 KIT    FILLER_COL  Kind::Int, plain           -- pads the page, and a neutral stop between
//                                                     RANGE_COL and SESSION_COL below
//   6 MIDI   SESSION_COL Kind::Int, range_dependent AND allow_session_from_horizontal -- the
//                                                     button-focus/already-focused-press pair

int32_t g_enum_col = 0;
constexpr uint16_t kEnumColNames[] = {sid(String::STRING_FOR_DISABLED), sid(String::STRING_FOR_ENABLED),
                                      sid(String::STRING_FOR_OFF)};
int32_t get_enum_col(const MenuContext&) {
	return g_enum_col;
}
void set_enum_col(const MenuContext&, int32_t v) {
	g_enum_col = v;
}

bool g_toggle_col = false;
int32_t get_toggle_col(const MenuContext&) {
	return g_toggle_col ? 1 : 0;
}
void set_toggle_col(const MenuContext&, int32_t v) {
	g_toggle_col = v != 0;
}

int32_t g_sub_leaf = 0;
int32_t get_sub_leaf(const MenuContext&) {
	return g_sub_leaf;
}
void set_sub_leaf(const MenuContext&, int32_t v) {
	g_sub_leaf = v;
}
constexpr Node sub_leaf =
    menu::int_item(sid(String::STRING_FOR_PAN), get_sub_leaf, set_sub_leaf, deluge::ui::oled::Range{0, 50, 1});
constexpr const Node* kSubmenuColChildren[] = {&sub_leaf};
constexpr Node submenu_col = menu::submenu(sid(String::STRING_FOR_FILTERS), kSubmenuColChildren);

int32_t g_chain_col = 0;
int32_t get_chain_col(const MenuContext&) {
	return g_chain_col;
}
void set_chain_col(const MenuContext&, int32_t v) {
	g_chain_col = v;
}
int32_t g_chain_target = 0;
int32_t get_chain_target(const MenuContext&) {
	return g_chain_target;
}
void set_chain_target(const MenuContext&, int32_t v) {
	g_chain_target = v;
}
constexpr Node chain_target = menu::int_item(sid(String::STRING_FOR_TEMPO), get_chain_target, set_chain_target,
                                             deluge::ui::oled::Range{0, 50, 1});
constexpr const Node* kChainColTargets[] = {&chain_target};
const Node* chain_col_chain(const MenuContext&, const menu::MenuState&) {
	return &chain_target;
}
constexpr Node chain_col = menu::with_select_chain(
    menu::int_item(sid(String::STRING_FOR_RESONANCE), get_chain_col, set_chain_col, deluge::ui::oled::Range{0, 50, 1}),
    chain_col_chain, kChainColTargets);

// The range-dependent column: a real `bind.param` so `permission_for`/`ensure_first_range` have a
// source to ask, exactly as `menu_scene_spec.cpp`'s own `transpose_leaf` is built.
constexpr Node range_col = menu::with_range_dependent(
    menu::param(sid(String::STRING_FOR_TRANSPOSE),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ}));

int32_t g_filler_col = 0;
int32_t get_filler_col(const MenuContext&) {
	return g_filler_col;
}
void set_filler_col(const MenuContext&, int32_t v) {
	g_filler_col = v;
}
constexpr Node filler_col =
    menu::int_item(sid(String::STRING_FOR_FEEDBACK), get_filler_col, set_filler_col, deluge::ui::oled::Range{0, 50, 1});

// A range-dependent column with `allow_session_from_horizontal` also set -- the port of one of
// `FileSelector`/`osc::AudioRecorder`/`sample::Start`/`sample::End`/`audio_clip::SampleMarkerEditor`
// (`Node::allow_session_from_horizontal`'s own doc). Shares `range_col`'s param on purpose: the two
// columns differ only in this one bit, so a test can hold everything else equal.
constexpr Node session_col = menu::with_allow_session_from_horizontal(menu::with_range_dependent(
    menu::param(sid(String::STRING_FOR_TRANSPOSE),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ})));

constexpr Node enum_col = menu::enumeration(sid(String::STRING_FOR_MODE), kEnumColNames, get_enum_col, set_enum_col);
constexpr Node toggle_col = menu::toggle_item(sid(String::STRING_FOR_MONOPHONIC), get_toggle_col, set_toggle_col);

constexpr const Node* kHorizChildren[] = {&enum_col,  &toggle_col, &submenu_col, &chain_col,
                                          &range_col, &filler_col, &session_col};
constexpr Node horiz_page = menu::submenu(sid(String::STRING_FOR_LPF), kHorizChildren, menu::Layout::Horizontal);

// A single four-slot value column, for the LED "one wide item" exception.
int32_t g_wide_col = 0;
int32_t get_wide_col(const MenuContext&) {
	return g_wide_col;
}
void set_wide_col(const MenuContext&, int32_t v) {
	g_wide_col = v;
}
constexpr Node wide_col_inner =
    menu::int_item(sid(String::STRING_FOR_MASTER_LEVEL), get_wide_col, set_wide_col, deluge::ui::oled::Range{0, 50, 1});
constexpr Node wide_col = menu::nodes::with_slots(wide_col_inner, 4);
constexpr const Node* kWideChildren[] = {&wide_col};
constexpr Node wide_page = menu::submenu(sid(String::STRING_FOR_MASTER_LEVEL), kWideChildren, menu::Layout::Horizontal);

// A vertical page, for the "all six off" example.
constexpr const Node* kVerticalChildren[] = {&chain_target};
constexpr Node vertical_page = menu::submenu(sid(String::STRING_FOR_SETTINGS), kVerticalChildren);

constexpr const Node* kRoots[] = {&horiz_page, &wide_page, &vertical_page};

// ---------------------------------------------------------------------------------------------
// Board fixture -- copied from `menu_scene_spec.cpp`'s `Rig`.

constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kGoldUpper = 4;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;
constexpr uint8_t kSynthX = 5, kSynthY = 0;
constexpr uint8_t kKitX = 5, kKitY = 1;
constexpr uint8_t kMidiX = 5, kMidiY = 2;
constexpr uint8_t kCvX = 5, kCvY = 3;
constexpr uint8_t kCrossScreenX = 6, kCrossScreenY = 2;
constexpr uint8_t kScaleX = 6, kScaleY = 0;

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const BoardProfile& p = deluge_profile()) : profile(p), fake(deluge::ui::shell::kDelugeBoard, p) {}

	MenuContext open(const Node& root) {
		const MenuContext ctx =
		    menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	void gold(uint8_t index, int16_t delta, uint32_t at_ms) { edges(index, delta, at_ms); }
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	void enter(uint32_t at_ms) { button(kSelectEncButtonX, kSelectEncButtonY, true, at_ms); }
	/// @brief One clean press-release of instrument button (\p x, \p y), settled by a tick.
	void press(uint8_t x, uint8_t y, uint32_t at_ms) {
		button(x, y, true, at_ms);
		shell.tick(at_ms + 5);
		button(x, y, false, at_ms + 10);
	}
};

int depth_of() {
	return menu_scene().state().depth;
}

std::string last_notice() {
	deluge::notify::Notice n;
	std::string text;
	while (deluge::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

/// @brief Whether \p f declared \p b's LED `On` this frame -- `LedFrame` has no query accessor of
///        its own, only `entries()`, so this scans it.
bool led_on(const deluge::ui::LedFrame& f, ButtonId b) {
	const auto id = static_cast<deluge::ui::LedId>(b);
	for (const auto& [entry_id, state] : f.entries()) {
		if (entry_id == id) {
			return state.kind == deluge::ui::LedState::Kind::On;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe menu_horizontal_controls("scenes::menu::MenuScene horizontal-page controls", $ {
	it("a select turn edits the focused column and leaves focus alone", _ {
		// `HorizontalMenu::selectEncoderAction` (`horizontal_menu.cpp:326`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_enum_col = 0;

		r.turn(1, 10);
		r.shell.tick(20);

		expect(g_enum_col).to_equal(1);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0); // still ENUM_COL
	});

	it("a select turn on a focused submenu column does nothing", _ {
		// `horizontal_menu.cpp:315-318`: `if (child->isSubmenu()) return;` -- no action at all.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		(void)last_notice(); // drain
		r.press(kMidiX, kMidiY, 200); // focus SUBMENU_COL (column 2); `press()`'s own internal
		                              // tick (`at_ms + 5`) is where a notice would be raised
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(2);
		// `Submenu::showNotification()` is false (`submenu.h:67`), and `displayNotification` returns
		// early for it (`horizontal_menu.cpp:336-338`) -- a button focus onto a submenu column raises
		// no banner, unlike every value column's own new-focus notification (see the KIT/toggle test
		// below).
		//
		// **Must be read right here, before any further tick.** `Shell::tick` drains
		// `notify::notices()` at the *start* of the next tick (`shell.cpp:327`), so an extra
		// `r.shell.tick(220)` here (as an earlier version of this test had) would silently eat any
		// notice raised at `press()`'s own internal tick before `last_notice()` ever read it --
		// making this assertion pass whether or not a banner was actually raised. Verified by
		// mutation: temporarily deleting `notify_column`'s `Kind::Submenu` guard made this fail
		// (see task-2-report.md's fix-round-2 section for the failing output).
		expect(last_notice().empty()).to_be_true();
		r.shell.tick(220);

		r.turn(1, 400);
		r.shell.tick(410);

		expect(depth_of()).to_equal(0); // no descend
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(2); // no move either
	});

	it("KIT focuses column 1; pressing KIT again flips the toggle and notifies", _ {
		// `handleInstrumentButtonPress`'s new-focus tail (`horizontal_menu.cpp:424-437`) and
		// `handleItemAction` (`:663-679`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_toggle_col = false;

		// `press()` already settles with its own internal tick; reading the notice after a
		// SECOND tick would drain it first (`Shell::tick` drains `notify::notices()` at the start
		// of the next tick, `shell.cpp:327`), so it must be read right after `press()` returns.
		(void)last_notice(); // drain
		r.press(kKitX, kKitY, 200); // new focus: notifies, does not flip
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		expect(g_toggle_col).to_be_false();
		expect(last_notice().empty()).to_be_false(); // new-focus notification

		(void)last_notice(); // drain
		r.press(kKitX, kKitY, 500); // already focused: the column's own press
		expect(g_toggle_col).to_be_true();
		expect(last_notice().empty()).to_be_false();
	});

	it("a select press on a focused toggle column flips and stays -- not ascend", _ {
		// `Toggle::selectButtonPress` (`gui/menu_item/toggle.h:38-42`): toggles and returns
		// `NO_NAVIGATION` when the page, not the toggle, is the current item, so
		// `SoundEditor::buttonAction`'s SELECT arm does nothing further (`sound_editor.cpp:394-419`)
		// -- no chain, no ascend. `Selection::selectButtonPress` (`gui/menu_item/selection.h:54-63`) has the
		// identical shape when `isToggle()` is true -- ported as `Node::selection_is_toggle`, whose
		// own doc names the three live examples on shipped horizontal pages
		// (`delay::PingPong`/`delay_ping_pong`, `randomizer::RandomizerLock`/`randomizer_lock`,
		// `sample::PitchSpeed`/`sample<i>_pitch_speed`); a dedicated example on `randomizer_lock`
		// covers that column kind below (`TOGGLE_COL` here is `Kind::Toggle` itself, not one of the
		// three). Every OTHER `NO_NAVIGATION`-returning `selectButtonPress` override in
		// `gui/menu_item/` either has no ported column kind (`ActiveScalesItem`, `PerformEditorItem`,
		// `ActionItem`, `ContextMenuItem`, `SourceSelectionItem`) or sits only on a vertical page in
		// the shipped tree (`Selection::isToggle()`'s other three overriders --
		// `arpeggiator::PresetMode`, `arpeggiator::IncludeInKitArp`, `runtime_feature::Setting`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_toggle_col = false;
		r.press(kKitX, kKitY, 200); // focus TOGGLE_COL (column 1)
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		r.enter(500);
		r.shell.tick(510);
		expect(g_toggle_col).to_be_true();
		expect(depth_of()).to_equal(0); // stayed -- no ascend
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		r.enter(800);
		r.shell.tick(810);
		expect(g_toggle_col).to_be_false(); // flips right back
		expect(depth_of()).to_equal(0);
	});

	it("a select press on the real randomizer_lock column flips and stays -- Selection::isToggle, "
	   "not Kind::Toggle",
	   _ {
		// `randomizer_lock` (`nodes/sound_randomizer.h`) is `Kind::Enum` with `Node::
		// selection_is_toggle` set, the port of `randomizer::RandomizerLock::isToggle() { return
		// true; }` (`gui/menu_item/randomizer/randomizer_lock.h:62`) -- one of the three live
		// examples `Node::selection_is_toggle`'s doc names, and the first column
		// (`kRandomizerChildren[0]`, SYNTH) of the real, shipped `randomizer` horizontal page.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		// Not `r.open()`: that assigns ids over this file's own local `kRoots`, not the real tree
		// `nodes::randomizer` lives in.
		const MenuContext ctx =
		    menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::randomizer, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::randomizer)});
		r.shell.tick(0);
		ctx.arp->randomizerLock = false;
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0); // default focus

		r.enter(100);
		r.shell.tick(110);
		expect(ctx.arp->randomizerLock).to_be_true();
		expect(depth_of()).to_equal(0); // stayed -- no ascend, no chain
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);

		r.enter(400);
		r.shell.tick(410);
		expect(ctx.arp->randomizerLock).to_be_false(); // flips right back
	});

	it("SYNTH focuses randomizer_lock; pressing SYNTH again flips it and notifies -- the "
	   "already-focused-press half",
	   _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx =
		    menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::randomizer, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::randomizer)});
		r.shell.tick(0);
		ctx.arp->randomizerLock = false;

		r.press(kKitX, kKitY, 200); // move focus off column 0 first (spread_gate, column 1)
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		(void)last_notice(); // drain
		r.press(kSynthX, kSynthY, 500); // new focus onto randomizer_lock: notifies, does not flip
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		expect(ctx.arp->randomizerLock).to_be_false();
		expect(last_notice().empty()).to_be_false();

		(void)last_notice(); // drain
		r.press(kSynthX, kSynthY, 800); // already focused: flips and notifies
		expect(ctx.arp->randomizerLock).to_be_true();
		expect(last_notice().empty()).to_be_false();
	});

	it("Back at depth 0 on a horizontal page cancels its notification too", _ {
		// `SoundEditor::goUpOneLevel` runs `endScreen()` (-> `HorizontalMenu::endSession`'s cancel)
		// **before** its depth-0 arm's `exitCompletely()` (`sound_editor.cpp:711-716`) -- the one
		// path `descend_to`/`ascend`'s own cancels do not cover, since neither runs for a Back that
		// closes the whole menu. Ships for real: `reroot_to_scene_opener_page()` lands a claimed
		// file selector's browser back at depth 0.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.turn(1, 100); // a select turn on ENUM_COL (column 0) notifies unconditionally
		r.shell.tick(110); // pump_input raises the notice (queued, not yet drained --
		                   // `Shell::tick` step 2 drains what a PRIOR tick queued, `shell.cpp:327`)
		r.shell.tick(120); // this tick's step 2 drains it, pushing `SceneKind::Console`
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_true();

		r.button(kBackX, kBackY, true, 200);
		r.button(kBackX, kBackY, false, 220);
		r.shell.tick(230); // pump_input runs the depth-0 Back arm, queuing `CancelConsole`
		r.shell.tick(240); // this tick's step 2 drains it, removing `SceneKind::Console`
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_false();
	});

	it("an unrelated Console notice survives a menu cancel", _ {
		// `ConsoleScene::cancel`'s own doc (fix round 2): Undo/Redo (`action_logger.cpp:827`,
		// `:840`) and the startup banner are `Kind::Console` too, raised with the default
		// `popup_type` (0, GENERAL) rather than `ui::shell::kPopupTypeNotification` (10) --
		// `notify_column` is the one raiser that uses the latter. Cancelling the menu's own
		// notification must not take an unrelated line down with it, and the scene itself must
		// stay up for as long as the unrelated line has left.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		(void)deluge::notify::notice_text("UNRELATED", deluge::notify::Notice::Kind::Console);
		r.shell.tick(50); // queues it
		r.shell.tick(60); // drains it -- pushes `SceneKind::Console`
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_true();

		r.turn(1, 100); // the menu's own notification, `kPopupTypeNotification`
		r.shell.tick(110);
		r.shell.tick(120);

		r.button(kBackX, kBackY, true, 200);
		r.button(kBackX, kBackY, false, 220);
		r.shell.tick(230); // queues `CancelConsole` for `kPopupTypeNotification` only
		r.shell.tick(240); // drains it -- the menu's own line goes inactive, "UNRELATED" does not
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_true();
	});

	it("two navigation presses inside 140 ms: the second is ignored", _ {
		// `horizontal_menu.cpp:77-82`, one debounce shared by all six buttons.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.press(kKitX, kKitY, 200); // accepted: focus -> column 1
		r.shell.tick(220);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		r.press(kSynthX, kSynthY, 250); // 50 ms later: inside the 140 ms window, ignored
		r.shell.tick(270);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1); // unchanged

		r.press(kSynthX, kSynthY, 450); // 200 ms after the accepted press: outside the window
		r.shell.tick(470);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
	});

	it("CROSS_SCREEN / SCALE page forward and back with wrap", _ {
		// `switchVisiblePage` (`horizontal_menu.cpp:345-366`), reached from `:85-94`.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(0);

		r.press(kCrossScreenX, kCrossScreenY, 200); // page forward: page 1 (RANGE_COL/FILLER_COL)
		r.shell.tick(220);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(1);

		r.press(kCrossScreenX, kCrossScreenY, 500); // forward again: wraps back to page 0
		r.shell.tick(520);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(0);

		r.press(kScaleX, kScaleY, 800); // backward: wraps to page 1
		r.shell.tick(820);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(1);
	});

	it("SHIFT + CROSS_SCREEN at depth 0 does nothing", _ {
		// Ruling 3: legacy switches the *chained* menu here, which the port has no data for; the
		// port does nothing rather than inventing a page switch legacy would not have done either.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.button(kShiftX, kShiftY, true, 100);
		r.press(kCrossScreenX, kCrossScreenY, 200);
		r.shell.tick(220);

		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(0); // unchanged
	});

	it("a select press on a value column follows its select_chain; with none, it ascends -- no notification either way",
	   _ {
		// `Submenu::selectButtonPress` (`submenu.cpp:187-194`) forwarded through
		// `SoundEditor::buttonAction`'s SELECT arm (`sound_editor.cpp:394-424`); no
		// `displayNotification` on this arm at all.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		r.press(kCvX, kCvY, 200); // focus CHAIN_COL (column 3), which has a select_chain
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(3);

		(void)last_notice();
		r.enter(500);
		r.shell.tick(510);
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &chain_target).to_be_true();
		expect(last_notice().empty()).to_be_true();

		// Back to the page. Column focus at depth 0 is untouched by the descent -- `ascend()`
		// restores the level's own cursor, and nothing wrote `columns[0]` while depth 1 was on
		// screen -- so CHAIN_COL is still focused; a select press here would just re-enter the
		// same chain. Move to ENUM_COL (no chain) with SYNTH before testing the ascend arm.
		r.button(kBackX, kBackY, true, 600);
		r.button(kBackX, kBackY, false, 620);
		r.shell.tick(630);
		expect(depth_of()).to_equal(0);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(3);

		r.press(kSynthX, kSynthY, 800); // ENUM_COL, no select_chain
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);

		(void)last_notice();
		r.enter(1000);
		r.shell.tick(1010);
		// `ascend()` at depth 0 is the port's own carried no-op (`menu_scene.cpp`'s note on
		// `ascend`), so this stays open -- the claim is only that nothing entered and nothing
		// notified.
		expect(depth_of()).to_equal(0);
		expect(last_notice().empty()).to_be_true();
	});

	it("button focus onto a range_dependent column runs the MustSelectRange check", _ {
		// `handleInstrumentButtonPress`'s `initializeItem` arm (`:424-433`), ported through
		// `enter_range_picker_if_needed`.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1); // two ranges: MustSelectRange, not Yes
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.press(kCrossScreenX, kCrossScreenY, 200); // page forward: `switchVisiblePage` keeps the
		                                            // on-page position, which lands focus on
		                                            // RANGE_COL (page 1, position 0) directly
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(1);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(4);

		// Move off RANGE_COL first, so the SYNTH press below is a genuine new-focus transition --
		// `handleItemAction`'s already-focused arm (tested above) does not run the range check at
		// all, only `handleInstrumentButtonPress`'s new-focus tail does.
		r.press(kKitX, kKitY, 500); // FILLER_COL, page 1 position 1
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(5);

		r.press(kSynthX, kSynthY, 800); // back onto RANGE_COL: a new focus this time

		// The picker opened instead of the column itself -- a real descent, one level deeper.
		expect(depth_of()).to_equal(1);
		expect(menu_scene().state().range_picker_pending).to_be_true();
		// And the page's own focus was still assigned to RANGE_COL first, exactly as
		// `handleInstrumentButtonPress` assigns `current_item_` before the range check.
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(4);
	});

	it("allow_session_from_horizontal: new focus skips MustSelectRange, an already-focused press "
	   "does not",
	   _ {
		// `Node::allow_session_from_horizontal`'s two call sites, both read on SESSION_COL --
		// identical to RANGE_COL except for this one bit:
		//   * `handleInstrumentButtonPress`'s new-focus tail (`horizontal_menu.cpp:427-428`):
		//     `MUST_SELECT_RANGE && !allowToBeginSessionFromHorizontalMenu()` -- the bit suppresses
		//     the picker entirely, so a first press onto this column just focuses it.
		//   * `handleItemAction`'s already-focused press (`:663-677`): the bit does NOT suppress
		//     the same check here -- permission is read unconditionally, and `MUST_SELECT_RANGE`
		//     still opens the picker, exactly as it would for an ordinary range-dependent column.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1); // two ranges: MustSelectRange, not Yes
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.press(kCrossScreenX, kCrossScreenY, 200); // page 1: RANGE_COL, FILLER_COL, SESSION_COL
		r.press(kKitX, kKitY, 500); // FILLER_COL first, so the MIDI press below is a genuine
		                           // new-focus transition onto SESSION_COL
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(5);

		r.press(kMidiX, kMidiY, 800); // new focus onto SESSION_COL: picker suppressed
		expect(depth_of()).to_equal(0);
		expect(menu_scene().state().range_picker_pending).to_be_false();
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(6);

		r.press(kMidiX, kMidiY, 1100); // already focused: permission checked unconditionally
		expect(depth_of()).to_equal(1);
		expect(menu_scene().state().range_picker_pending).to_be_true();
	});

	it("LEDs: SYNTH/KIT/MIDI/CV light the focused column's slots", _ {
		// `updateSelectedMenuItemLED` (`horizontal_menu.cpp:546-572`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		expect(led_on(f, ButtonId::Synth)).to_be_true(); // ENUM_COL is focused, column 0
		expect(led_on(f, ButtonId::Kit)).to_be_false();
		expect(led_on(f, ButtonId::Midi)).to_be_false();
		expect(led_on(f, ButtonId::Cv)).to_be_false();
	});

	it("LEDs: SCALE and CROSS_SCREEN light only with more than one page", _ {
		// `renderOLED`'s own two-line LED update (`:118-119`).
		select_language();
		deluge::notify::dirty().drain();
		{
			Rig r;
			(void)r.open(horiz_page); // two pages
			r.shell.tick(0);
			deluge::ui::LedFrame f;
			menu_scene().leds(f);
			expect(led_on(f, ButtonId::Scale)).to_be_true();
			expect(led_on(f, ButtonId::CrossScreen)).to_be_true();
		}
		{
			Rig r;
			(void)r.open(wide_page); // one page, one wide column
			r.shell.tick(0);
			deluge::ui::LedFrame f;
			menu_scene().leds(f);
			expect(led_on(f, ButtonId::Scale)).to_be_false();
			expect(led_on(f, ButtonId::CrossScreen)).to_be_false();
		}
	});

	it("LEDs: a single four-slot value item lights none of SYNTH/KIT/MIDI/CV", _ {
		// `updateSelectedMenuItemLED`'s own single-item exception: `page_items.size() > 1 ||
		// page_items[0]->isSubmenu() || page_items[0]->getOccupiedSlots() < 4` is false here, so
		// `led_states` stays all-false.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(wide_page);
		r.shell.tick(0);

		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		expect(led_on(f, ButtonId::Synth)).to_be_false();
		expect(led_on(f, ButtonId::Kit)).to_be_false();
		expect(led_on(f, ButtonId::Midi)).to_be_false();
		expect(led_on(f, ButtonId::Cv)).to_be_false();
	});

	it("LEDs: all six off on a vertical page", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(vertical_page);
		r.shell.tick(0);

		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		for (ButtonId b : {ButtonId::Synth, ButtonId::Kit, ButtonId::Midi, ButtonId::Cv, ButtonId::CrossScreen,
		                   ButtonId::Scale}) {
			expect(led_on(f, b)).to_be_false();
		}
	});
});
// clang-format on
DELUGE_SPEC(menu_horizontal_controls_spec, menu_horizontal_controls)
