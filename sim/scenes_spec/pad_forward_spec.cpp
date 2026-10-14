/// @file
/// @brief `gui::ui::forwardGridPad` -- `SoundEditor::padAction`'s root switch
///        (`gui/ui/sound_editor.cpp:1474-1517`) and its tail (`:1520-1545`), ported.
///
/// What this pins: the six-way decision `forwardGridPad` makes over the real legacy root-UI
/// singletons (`keyboardScreen`, `instrumentClipView`, `automationView`, `performanceView`, and the
/// tail's fallback), through a recording `perform` installed by `setPadForwardPerform` -- so an
/// example can assert the decision, and the coordinates the target would have been handed, without
/// running a legacy view's own pad handler. Two exceptions run for real instead of through the
/// recorder: "really removes the ported menu when the tail fires" restores the real `perform` and
/// runs the real tail arm (`closePortedMenu`) against a real `MenuScene` on the real guest's scene
/// stack -- that call names no legacy view, so it is safe to run for real here, and it is what makes
/// behaviour 3 (the tail firing `closePortedMenu`) covered outside the recorder; the four
/// `automationView` examples open a real `MenuScene` (via `open_menu_node`, on `menu_scene()`
/// directly rather than on the guest's stack) because `menuShowsAutomationParam` reads
/// `menu_scene().focused()` -- there is no other way to give it an answer.
///
/// The two note-mode arms (`sound_editor.cpp:1481-1498`, note-family Task 7) are asked of a real
/// `MenuScene` opened at `Root::NoteEditor` / `Root::NoteRowEditor` on the global guest's own stack,
/// because `activeNoteMode()` reads exactly that. Their performer is pinned without running either
/// legacy handler: `setNoteModeLegacyCalls` replaces the two `instrumentClipView` calls with
/// recorders. (Task 7a gated both handlers' `soundEditor` navigation calls -- the legacy arms are now
/// `gui/views/instrument_clip_view.cpp:3677`, `:3894`, `:3905`, `:3946` -- and `note_mode_reads_spec.cpp` runs them.)
///
/// What this deliberately does not cover: the tail's firmware-version diagnostics, out of scope for
/// this switch -- `shell_pad_forward.h`'s own note says why. And nothing here asks
/// `Scene::grid_pad_claim()` (Task 1): that question is asked by this function's caller, a later
/// task, before `forwardGridPad` is ever reached.

#include "gui/ui/shell_pad_forward.h"

#include "definitions_cxx.hpp" // kDisplayWidth
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_host_ui.h" // shellHostUI
#include "gui/ui/shell_note_mode.h"
#include "gui/ui/ui.h" // setRootUILowLevel, nullifyUIs, currentUIMode, UI_MODE_*
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"

#include "modulation/params/param.h" // params::Kind, LOCAL_LPF_FREQ, UNPATCHED_BASS -- task 3's param nodes
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h" // SceneKind

#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::gui::ui::PadForwardTarget;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::scenes::menu::nodes;
namespace params = deluge::modulation::params;
using deluge::scenes::spec::GlobalGuestGuard;
using deluge::scenes::spec::open_menu_on_global_guest_at;
using deluge::scenes::spec::UiStackGuard;

namespace {

/// @brief Open a real `MenuScene` at `nodes::root_song` on the global guest's OWN scene stack --
///        `menu_actions_spec.cpp`'s `open_global_guest`, narrowed to the one root this file needs.
///
/// `root_song` needs no clip (`context_for`'s second argument is `nullptr`), so this needs nothing
/// from `spec::SongFixture` but the `Song&` itself -- unlike `sound_root`, which task 1's spec fixture
/// resolves a clip and instrument for.
///
/// No `openUI` here, deliberately, matching `open_global_guest`'s own note: this only needs the menu
/// on the guest's *scene* stack, not the guest on `uiNavigationHierarchy` -- the root UI for the one
/// example that calls this is seated separately, with `setRootUILowLevel`.
bool open_menu_on_global_guest(Song& song) {
	const MenuContext ctx = menu::context_for(song, nullptr, 0, false,
	                                          /*session_or_arranger_root=*/true, /*session_view_root=*/true,
	                                          /*kit_global_fx=*/false);
	static const Node* const roots[] = {&nodes::root_song};
	(void)menu::assign_node_ids(roots);
	menu_scene().open(nodes::root_song, ctx);
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::root_song)});
	return shellHostUI.opened();
}

/// @brief What the two note-mode pad handlers were handed, standing in for `instrumentClipView`'s.
struct NotePadSeen {
	int calls = 0;
	int32_t x = -1;
	int32_t y = -1;
	int32_t velocity = -1;
};
NotePadSeen g_edit_pad{};
NotePadSeen g_row_pad{};
/// What the recording `noteRowEditorPad` answers -- legacy's `handled`.
bool g_row_pad_handled = true;

ActionResult unused_button(deluge::hid::Button, bool, bool) {
	return ActionResult::DEALT_WITH;
}
ActionResult unused_horizontal(int32_t) {
	return ActionResult::DEALT_WITH;
}
ActionResult unused_vertical(int32_t, bool) {
	return ActionResult::DEALT_WITH;
}
void unused_exit() {
}

/// @brief Every legacy note-mode call, with the two pad handlers recording and the rest inert.
const deluge::gui::ui::NoteModeLegacyCalls kRecordingNotePads{
    .noteEditorButton = unused_button,
    .noteRowEditorButton = unused_button,
    .noteEditorHorizontal = unused_horizontal,
    .noteRowEditorHorizontal = unused_horizontal,
    .noteEditorVertical = unused_vertical,
    .noteRowEditorVertical = unused_vertical,
    .noteEditorEditPad = [](int32_t x, int32_t y, int32_t v) { g_edit_pad = {g_edit_pad.calls + 1, x, y, v}; },
    .noteRowEditorPad =
        [](int32_t x, int32_t y, int32_t v) {
	        g_row_pad = {g_row_pad.calls + 1, x, y, v};
	        return g_row_pad_handled;
        },
    .exitNoteEditor = unused_exit,
    .exitNoteRowEditor = unused_exit,
};

/// @brief Install `kRecordingNotePads` for one example, and put the real calls back however it ends.
struct RecordingNotePads {
	RecordingNotePads(const RecordingNotePads&) = delete;
	RecordingNotePads& operator=(const RecordingNotePads&) = delete;
	RecordingNotePads() {
		g_edit_pad = NotePadSeen{};
		g_row_pad = NotePadSeen{};
		g_row_pad_handled = true;
		deluge::gui::ui::setNoteModeLegacyCalls(&kRecordingNotePads);
	}
	~RecordingNotePads() { deluge::gui::ui::setNoteModeLegacyCalls(nullptr); }
};

/// @brief `menu_scene_spec.cpp`'s `Rig::open`, narrowed to a bare node rather than a root: the
///        four `menuShowsAutomationParam` examples below need `menu_scene().focused()` to answer a
///        specific node, not a working menu tree, so this skips `assign_node_ids` and the shell
///        stack entirely -- none of them asks `Scene::grid_pad_claim()` of these nodes.
void open_menu_node(Song& song, InstrumentClip* clip, const Node& node) {
	const MenuContext ctx = menu::context_for(song, clip, 0, false, /*session_or_arranger_root=*/false,
	                                          /*session_view_root=*/false, /*kit_global_fx=*/false);
	menu_scene().open(node, ctx);
}

/// @brief A `Kind::Toggle` leaf: `bind.param` stays default-constructed (`ParamRef{}`, invalid), so
///        this is "a node with no param binding" for the sidebar example.
bool g_toggle_state = false;
constexpr Node no_param_node = menu::toggle_item(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ENABLED),
    [](const MenuContext&) { return static_cast<int32_t>(g_toggle_state); },
    [](const MenuContext&, int32_t v) { g_toggle_state = v != 0; });

/// @brief An ordinary patched param -- `bindings_spec.cpp`'s `lpf_freq_node`. Used both as the
///        "matches" node (paired with a clip set to the same kind/index) and the "does not match"
///        one (paired with a clip left at its default `Kind::NONE` / `kNoParamID`, or explicitly
///        set to a different param).
constexpr Node param_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_FREQUENCY),
                                        {params::Kind::PATCHED, params::LOCAL_LPF_FREQ});

} // namespace

// clang-format off
describe pad_forward("gui::ui::forwardGridPad -- SoundEditor::padAction's root switch", $ {
	// The recording perform. `forwardGridPad` returns its decision too; the recorder exists so an
	// example can also assert the coordinates the target would have been handed.
	static PadForwardTarget seen;
	static int32_t seen_x, seen_y, seen_v;

	before_each([] {
		seen = PadForwardTarget::None;
		deluge::gui::ui::setPadForwardPerform([](PadForwardTarget t, int32_t x, int32_t y, int32_t v) {
			seen = t; seen_x = x; seen_y = y; seen_v = v;
		});
	});
	after_each([] { deluge::gui::ui::setPadForwardPerform(nullptr); });

	it("sends every pad to the keyboard screen when that is the root", _ {
		// `sound_editor.cpp:1474-1477`: the arm has no coordinate test at all.
		UiStackGuard ui_guard;
		setRootUILowLevel(&keyboardScreen);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Keyboard);
		expect(seen_x).to_equal(3);
	});

	it("sends the audition column, and only that column, to the clip view", _ {
		// `:1499-1502`: `x == kDisplayWidth + 1`, nothing else.
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64))
		    .to_equal(PadForwardTarget::AuditionColumn);
	});

	it("lets a main-grid pad on a clip root fall through to the tail", _ {
		// `:1500`'s `else if` not taken -> the comment `// fall through below` at `:1504` -> the
		// tail at `:1520-1545`. M3: this is NOT the note-editor arms -- no ported menu is on the
		// global guest's stack here, so `activeNoteMode()` answers `None`, which is the port of
		// neither `inNoteEditor()` nor `inNoteRowEditor()` holding. (When this example was written
		// no guest path could set either mode; note-family Task 7 ported both arms, and the examples
		// under "the note modes" below are theirs.) This example is ordinary behaviour 3 (§2.1) at
		// site 5/one of site 6's fan-out arms: `instrumentClipView` matches no coordinate-gated arm
		// above it for a plain main-grid pad, so both legacy and the port fall to the tail the same
		// way -- no divergence in this example.
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("sends everything to the performance view when that is the root", _ {
		// `:1515-1517`, which returns performanceView's own result and never reaches the tail.
		UiStackGuard ui_guard;
		setRootUILowLevel(&performanceView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Performance);
	});

	it("does not exit on a release", _ {
		// `:1520`'s `&& on`. Load-bearing: a swallowed release leaves legacy's audition state
		// latched after a forwarded audition-column press.
		UiStackGuard ui_guard;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 0)).to_equal(PadForwardTarget::None);
	});

	it("does not exit while a UI mode is active", _ {
		// `:1520`'s `currentUIMode == UI_MODE_NONE`.
		UiStackGuard ui_guard;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::None);
		currentUIMode = UI_MODE_NONE;
	});

	it("does not exit on the three firmware-version diagnostic pads (I2)", _ {
		// `sound_editor.cpp:1521-1543`: while `&firmwareVersionMenu` is open, `y == 7` and `x` in
		// {13, 14, 15} return `DEALT_WITH` **without** falling to `exitCompletely()` -- the popups
		// themselves stay unported (`shell_pad_forward.h`'s own note), but the "menu stays open"
		// half is legacy's own behaviour and is what this pins. `open_menu_node` focuses the ported
		// menu on `nodes::firmware_version` directly, the same shape the automation examples above
		// use for their own node.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, nodes::firmware_version);
		setRootUILowLevel(&sessionView); // any root with no matching forwarding arm
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(15, 7, 64)).to_equal(PadForwardTarget::None); // SRAM
		expect(deluge::gui::ui::forwardGridPad(14, 7, 64)).to_equal(PadForwardTarget::None); // voices
		expect(deluge::gui::ui::forwardGridPad(13, 7, 64)).to_equal(PadForwardTarget::None); // PIC ver

		// Control: a pad this carve-out does not name, on the same open menu, still exits -- the
		// clause is additive, not a blanket "firmware-version menu never exits".
		expect(deluge::gui::ui::forwardGridPad(12, 7, 64)).to_equal(PadForwardTarget::Exit);
		expect(deluge::gui::ui::forwardGridPad(15, 6, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("does exit on the same three pads when a different node is focused", _ {
		// The carve-out is keyed on node identity, not on coordinate alone -- without
		// `onFirmwareVersionMenu()`'s check, any menu open at `y == 7` would swallow these three
		// pads regardless of what it was showing.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, no_param_node);
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(15, 7, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("really removes the ported menu when the tail fires", _ {
		// The one example that runs the REAL perform: `closePortedMenu()` names no legacy view, so
		// it is safe here, and this is what makes behaviour 3 covered outside the recorder.
		deluge::gui::ui::setPadForwardPerform(nullptr);
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest(*fx.song)).to_be_true();
		expect(shellHostUI.shell().stack().depth() > 0u).to_be_true();

		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		(void)deluge::gui::ui::forwardGridPad(3, 4, 64);
		expect(shellHostUI.shell().stack().depth()).to_equal(0u);
	});

	// ---- the note modes (`sound_editor.cpp:1481-1498`) ----------------------------------------------

	it("under the note editor over the clip view, sends a main-grid pad to its edit action and a "
	   "sidebar pad to Exit", _ {
		// The edit action's own `if (on)` is the handler's (`instrument_clip_view.cpp:3644`). The arm
		// itself, `sound_editor.cpp:1481-1490`, tests neither `on` nor `currentUIMode`, and its exit is
		// unconditional -- so a release and a UI mode take the same arms, which is what separates this
		// Exit from the tail's (`:1520`), and the audition column is the note arm's, not `:1499`'s.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteEditor)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::NoteEditorEdit).to_be_true();
		expect(seen == PadForwardTarget::NoteEditorEdit).to_be_true();
		expect(seen_x).to_equal(3);
		expect(seen_y).to_equal(4);
		expect(seen_v).to_equal(64);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth - 1, 7, 0) == PadForwardTarget::NoteEditorEdit)
		    .to_be_true();

		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 64) == PadForwardTarget::Exit).to_be_true();
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64) == PadForwardTarget::Exit).to_be_true();
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 0) == PadForwardTarget::Exit).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 2, 64) == PadForwardTarget::Exit).to_be_true();
		currentUIMode = UI_MODE_NONE;
	});

	it("under the note-row editor over the clip view, sends every pad to the note-row editor", _ {
		// `sound_editor.cpp:1492-1497`: no coordinate test, no `on` test, no mode test -- the handler decides, and
		// the note-row editor normally runs with a row auditioned (`UI_MODE_AUDITIONING`).
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteRowEditor)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::NoteRowEditor).to_be_true();
		expect(seen_x).to_equal(3);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 64) == PadForwardTarget::NoteRowEditor)
		    .to_be_true();
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64) == PadForwardTarget::NoteRowEditor)
		    .to_be_true();
		expect(deluge::gui::ui::forwardGridPad(3, 4, 0) == PadForwardTarget::NoteRowEditor).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::NoteRowEditor).to_be_true();
		currentUIMode = UI_MODE_NONE;
	});

	it("takes the note arms only while the ported menu is on the guest's stack", _ {
		// Control for the two examples above, varying one thing: the same menu, same root and same
		// root UI, with the menu removed from the stack. `menu_context().root` still reads
		// `NoteEditor`; the arms must not.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteEditor)).to_be_true();
		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(menu_scene().menu_context().root == MenuContext::Root::NoteEditor).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::Exit).to_be_true(); // the tail
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64) == PadForwardTarget::AuditionColumn)
		    .to_be_true();
	});

	it("keeps the note arms inside the clip view's case: another root takes its own arm", _ {
		// `sound_editor.cpp:1474` comes before `:1480`, and `:1515` is a separate root: the note arms are nested in
		// `rootUI == &instrumentClipView`, so a note root over another root view changes nothing.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteRowEditor)).to_be_true();
		currentUIMode = UI_MODE_NONE;

		setRootUILowLevel(&keyboardScreen);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::Keyboard).to_be_true();
		setRootUILowLevel(&performanceView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::Performance).to_be_true();
		setRootUILowLevel(&sessionView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64) == PadForwardTarget::Exit).to_be_true();
	});

	it("the real performer hands a NoteEditorEdit pad to the note editor's edit action and leaves the "
	   "menu open", _ {
		// `sound_editor.cpp:1484`, then `:1490`'s `return DEALT_WITH` -- no exit. The sidebar half is `:1488`'s
		// `exitCompletely()`, which is the `Exit` arm's `closePortedMenu()`.
		deluge::gui::ui::setPadForwardPerform(nullptr);
		RecordingNotePads recording;
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteEditor)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		(void)deluge::gui::ui::forwardGridPad(3, 4, 64);
		expect(g_edit_pad.calls).to_equal(1);
		expect(g_edit_pad.x).to_equal(3);
		expect(g_edit_pad.y).to_equal(4);
		expect(g_edit_pad.velocity).to_equal(64);
		expect(shellHostUI.shell().stack().depth()).to_equal(1u);

		(void)deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 64);
		expect(g_edit_pad.calls).to_equal(1);
		expect(shellHostUI.shell().stack().depth()).to_equal(0u);
	});

	it("the real performer closes the menu when the note-row editor's handler returns false, and "
	   "only then", _ {
		// Both answers are the recorder's, because the real handler returns true on every path
		// (`instrument_clip_view.cpp:3850-3867`) -- but the false arm is legacy's, kept rather than
		// folded away: `sound_editor.cpp:1493-1496`, `bool handled = ...; if (!handled) {
		// exitCompletely(); }`.
		deluge::gui::ui::setPadForwardPerform(nullptr);
		RecordingNotePads recording;
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteRowEditor)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		g_row_pad_handled = true;
		(void)deluge::gui::ui::forwardGridPad(3, 4, 64);
		expect(g_row_pad.calls).to_equal(1);
		expect(g_row_pad.x).to_equal(3);
		expect(g_row_pad.y).to_equal(4);
		expect(g_row_pad.velocity).to_equal(64);
		expect(shellHostUI.shell().stack().depth()).to_equal(1u);

		// A main-grid release, deliberately: without the note arm this pad reaches the tail rather
		// than `AuditionColumn`'s real `instrumentClipView.padAction`, so a missing arm fails the
		// expectations below instead of taking the whole binary down with it.
		g_row_pad_handled = false;
		(void)deluge::gui::ui::forwardGridPad(5, 2, 0);
		expect(g_row_pad.calls).to_equal(2);
		expect(g_row_pad.x).to_equal(5);
		expect(g_row_pad.velocity).to_equal(0);
		expect(shellHostUI.shell().stack().depth()).to_equal(0u);
	});

	it("sends a sidebar pad to automation view regardless of the param", _ {
		// `:1553-1557`: `(x >= kDisplayWidth) || editingParamInAutomationView` -- the sidebar half
		// needs no param agreement at all.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, no_param_node);
		setRootUILowLevel(&automationView);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 2, 64))
		    .to_equal(PadForwardTarget::Automation);
	});

	it("sends a main-grid pad to automation view when the menu shows the same param", _ {
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::PATCHED;
		fx.clip->lastSelectedParamID = params::LOCAL_LPF_FREQ;
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Automation);
	});

	it("falls through to the tail when the menu shows a different param", _ {
		// `handleAutomationViewPadAction` returns NOT_DEALT_WITH (`:1561`), and `:1508-1511` then
		// does not return -- so legacy reaches the tail.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
		fx.clip->lastSelectedParamID = params::UNPATCHED_BASS;
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("does not dereference a null clip", _ {
		// The guard migration §10.5 banked for legacy's copy (`:1582-1586` dereferences
		// `getCurrentClip()` unguarded). This is the port's own copy, so writing the guard here is
		// not the banked fix -- legacy's copy is untouched.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.song->setCurrentClip(nullptr); // no current clip -- `getCurrentClip()` returns nullptr
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});
});
// clang-format on

DELUGE_SPEC(pad_forward_spec, pad_forward)
