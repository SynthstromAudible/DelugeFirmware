/// @file
/// @brief `gui::ui::forwardGridPad` -- `SoundEditor::padAction`'s root switch
///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) and its tail, ported.
///
/// What this pins: the six-way decision `forwardGridPad` makes over the real legacy root-UI
/// singletons (`keyboardScreen`, `instrumentClipView`, `automationView`, `performanceView`, and the
/// tail's fallback), through a recording `perform` installed by `setPadForwardPerform` -- so an
/// example can assert the decision, and the coordinates the target would have been handed, without
/// running a legacy view's own pad handler. Two exceptions run for real instead of through the
/// recorder: "really removes the menu when the tail fires" restores the real `perform` and
/// runs the real tail arm (`closePortedMenu`) against a real `MenuScene` on the real guest's scene
/// stack -- that call names no legacy view, so it is safe to run for real here, and it is what covers
/// the tail firing `closePortedMenu` outside the recorder; the `automationView` examples open a real
/// `MenuScene` because `menuShowsAutomationParam` reads `menu_scene().focused_automation_param()` --
/// there is no other way to give it an answer. A bare leaf opens on `menu_scene()` directly
/// (`open_menu_node`); a horizontal page focused on a column, a cable's page and the pad-claim examples
/// open through the shortcut entry (`open_shortcut_over`), on the guest's own stack.
///
/// The two note-mode arms (`SoundEditor::padAction` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`) are asked of a
/// real `MenuScene` opened at `Root::NoteEditor` / `Root::NoteRowEditor` on the global guest's own stack, because
/// `activeNoteMode()` reads exactly that. Their performer is pinned without running either legacy handler:
/// `setNoteModeLegacyCalls` replaces the two `instrumentClipView` calls with recorders. (Both handlers' `soundEditor`
/// navigation calls -- `readValueAgain`, `goUpOneLevel` and `toggleNoteEditorParamMenu` in
/// `gui/views/instrument_clip_view.cpp` -- are gated on the note mode, and `note_mode_reads_spec.cpp` runs
/// them.)
///
/// Nothing here asks
/// `Scene::grid_pad_claim()`: that question is asked by this function's caller, before `forwardGridPad` is ever
/// reached.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_automation_glue.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_pad_forward.h"
#include "gui/views/arranger_view.h"
#include "hid/led/indicator_leds.h"
#include "model/instrument/kit.h"
#include "model/settings/runtime_feature_settings.h"
#include "processing/sound/sound_drum.h"

#include "definitions_cxx.hpp" // kDisplayWidth
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/shell_host_ui.h" // shellHostUI
#include "gui/ui/shell_note_mode.h"
#include "gui/ui/ui.h" // setRootUILowLevel, nullifyUIs, currentUIMode, UI_MODE_*
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "model/edits/setting_edit.h"
#include "processing/sound/sound_instrument.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "extern.h" // picFirmwareVersion
#include "memory/heaps.h"
#include "modulation/params/param.h" // params::Kind, LOCAL_LPF_FREQ, UNPATCHED_BASS -- the param nodes
#include "processing/engines/audio_engine.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/rename/rename_scene.h"
#include "ui/scenes/slicing/slicer_scene.h"
#include "ui/shell/scene.h" // SceneKind

#include "support/global_guest_menu.h"
#include "support/menu_over_root.h"
#include "support/song_fixture.h"

#include "support/printers/rgb_printer.h"
#include "synth/modulation/cable.h" // CableTarget -- a cable's id in the automation view's numbering
#include "util/menu_entry.h"        // openMenuAtShortcut
#include "util/sound_menu_open.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::gui::ui::PadForwardTarget;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::ui::scenes::menu::nodes;
namespace params = deluge::modulation::params;
using deluge::ui::scenes::spec::GlobalGuestGuard;
using deluge::ui::scenes::spec::open_menu_on_global_guest_at;
using deluge::ui::scenes::spec::open_shortcut_over;
using deluge::ui::scenes::spec::open_shortcut_over_automation;
using deluge::ui::scenes::spec::ShiftHeld;
using deluge::ui::scenes::spec::UiStackGuard;

namespace indicator_leds {
/// Legacy's LED shadow array (`hid/led/indicator_leds.cpp`), which `setLedState` writes; `indicator_leds.h` does not
/// declare it.
extern bool ledStates[];
} // namespace indicator_leds

namespace {

/// @brief Whether legacy's @p led is lit, as `setLedState` last set it.
bool led_on(IndicatorLED led) {
	return indicator_leds::ledStates[static_cast<uint8_t>(led)];
}

/// The vibrato page's cable, LFO 1 onto pitch, in the automation view's numbering: the cable's descriptor word.
constexpr int32_t kVibratoCableId = static_cast<int32_t>(
    deluge::synth::CableTarget::depth_of(params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1).word);

/// @brief Open a real `MenuScene` at `nodes::root_song` on the global guest's OWN scene stack --
///        `menu_actions_spec.cpp`'s `open_global_guest`, narrowed to the one root this file needs.
///
/// `root_song` needs no clip (the view names none: `menuViewOf(nullptr)`), so this needs nothing
/// from `spec::SongFixture` but the `Song&` itself -- unlike `sound_root`, which the spec fixture
/// resolves a clip and instrument for.
///
/// No `openUI` here, deliberately, matching `open_global_guest`'s own note: this only needs the menu
/// on the guest's *scene* stack, not the guest on `uiNavigationHierarchy` -- the root UI for the one
/// example that calls this is seated separately, with `setRootUILowLevel`.
bool open_menu_on_global_guest(Song& song) {
	const MenuContext ctx = spec::ctx_of(song, deluge::gui::menuViewOf(nullptr), 0, false,
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

ActionResult unused_button(deluge::hid::Button, bool) {
	return ActionResult::DEALT_WITH;
}
ActionResult unused_horizontal(int32_t) {
	return ActionResult::DEALT_WITH;
}
ActionResult unused_vertical(int32_t) {
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
	const MenuContext ctx =
	    spec::ctx_of(song, deluge::gui::menuViewOf(clip), 0, false, /*session_or_arranger_root=*/false,
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
constexpr Node param_node = menu::param(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_FREQUENCY),
    deluge::ui::scenes::menu::sound_param(deluge::plugin::ParamId{static_cast<uint32_t>(params::LOCAL_LPF_FREQ)}));

} // namespace

extern int32_t numUIsOpen;                  // `gui/ui/ui.cpp`: how many UIs are open
extern uint32_t whichMainRowsNeedRendering; // `gui/ui/ui.cpp`: the main rows a render is pending for
extern uint32_t whichSideRowsNeedRendering; // `gui/ui/ui.cpp`: the sidebar rows a render is pending for

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
		// `SoundEditor::padAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the `keyboardScreen` arm has no
		// coordinate test at all.
		UiStackGuard ui_guard;
		setRootUILowLevel(&keyboardScreen);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Keyboard);
		expect(seen_x).to_equal(3);
	});

	it("sends the audition column, and only that column, to the clip view", _ {
		// main's `SoundEditor::padAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): `x == kDisplayWidth + 1`,
		// nothing else.
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64))
		    .to_equal(PadForwardTarget::AuditionColumn);
	});

	it("hands the marker's audition column to the clip view on an instrument clip, whatever the root", _ {
		// main's `SampleMarkerEditor::padAction` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`): no root
		// test, only the clip's type.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		setRootUILowLevel(&keyboardScreen);
		deluge::gui::ui::forwardClipAuditionPad(kDisplayWidth + 1, 4, 64);
		expect(seen).to_equal(PadForwardTarget::AuditionColumn);
		expect(seen_v).to_equal(64);
	});

	it("swallows the marker's audition column on an audio clip", _ {
		UiStackGuard ui_guard;
		spec::AudioSongFixture fx = spec::make_audio_clip_song();
		setRootUILowLevel(&instrumentClipView);
		deluge::gui::ui::forwardClipAuditionPad(kDisplayWidth + 1, 4, 64);
		expect(seen).to_equal(PadForwardTarget::None);
	});

	it("lets a main-grid pad on a clip root fall through to the tail", _ {
		// The audition-column `else if` of main's `SoundEditor::padAction` not taken -> its `// fall through below`
		// comment -> the tail. This is NOT the note-editor arms -- no menu is on the global guest's stack here, so
		// `activeNoteMode()` answers `None`, which is the port of neither `inNoteEditor()` nor `inNoteRowEditor()`
		// holding. (The examples under "the note modes" below cover the note-editor arms.) This example is ordinary
		// behaviour: `instrumentClipView` matches no coordinate-gated arm above it for a plain main-grid pad, so both
		// legacy and the port fall to the tail the same way -- no divergence in this example.
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("sends everything to the performance view when that is the root", _ {
		// main's `SoundEditor::padAction` returns `performanceView.padAction`'s own result for this root and never
		// reaches the tail.
		UiStackGuard ui_guard;
		setRootUILowLevel(&performanceView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Performance);
	});

	it("does not exit on a release", _ {
		// The tail's `&& on` in main's `SoundEditor::padAction`. Load-bearing: a swallowed release leaves legacy's
		// audition state latched after a forwarded audition-column press.
		UiStackGuard ui_guard;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 0)).to_equal(PadForwardTarget::None);
	});

	it("does not exit while a UI mode is active", _ {
		// The tail's `currentUIMode == UI_MODE_NONE` in main's `SoundEditor::padAction`.
		UiStackGuard ui_guard;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::None);
		currentUIMode = UI_MODE_NONE;
	});

	it("answers the three firmware-version diagnostic pads with their popups, and does not exit", _ {
		// `SoundEditor::padAction`'s tail (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): while `&firmwareVersionMenu`
		// is open, `y == 7` and `x` in {13, 14, 15} pop up the PIC version, the voice count and the internal memory, and
		// return `DEALT_WITH` without falling to `exitCompletely()`.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, nodes::firmware_version);
		setRootUILowLevel(&sessionView); // any root with no matching forwarding arm
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(15, 7, 64)).to_equal(PadForwardTarget::FirmwareDiagnostic); // SRAM
		expect(deluge::gui::ui::forwardGridPad(14, 7, 64)).to_equal(PadForwardTarget::FirmwareDiagnostic); // voices
		expect(deluge::gui::ui::forwardGridPad(13, 7, 64)).to_equal(PadForwardTarget::FirmwareDiagnostic); // PIC
		// A release is not a press: swallowed.
		expect(deluge::gui::ui::forwardGridPad(14, 7, 0)).to_equal(PadForwardTarget::None);

		// Control: a pad this carve-out does not name, on the same open menu, still exits -- the
		// clause is additive, not a blanket "firmware-version menu never exits".
		expect(deluge::gui::ui::forwardGridPad(12, 7, 64)).to_equal(PadForwardTarget::Exit);
		expect(deluge::gui::ui::forwardGridPad(15, 6, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("pops up the diagnostic each pad names, as main's displayPopup of the number", _ {
		deluge::gui::ui::setPadForwardPerform(nullptr);
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, nodes::firmware_version);
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;
		const auto popped = [](int32_t x) {
			deluge::base::notify::Notice n;
			while (deluge::base::notify::notices().pop(n)) {} // what was queued before
			(void)deluge::gui::ui::forwardGridPad(x, 7, 64);
			std::string text;
			while (deluge::base::notify::notices().pop(n)) {
				if (n.kind == deluge::base::notify::Notice::Kind::Text) {
					text = n.text;
				}
			}
			return text;
		};
		expect(popped(15)).to_equal(std::to_string(deluge::memory::sram_size()));
		expect(popped(14)).to_equal(std::to_string(AudioEngine::getNumVoices() + AudioEngine::getNumAudio()));
		expect(popped(13)).to_equal(std::to_string(picFirmwareVersion));
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::firmware_version);
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

	it("really removes the menu scene when the tail fires", _ {
		// The one example that runs the REAL perform: `closePortedMenu()` names no legacy view, so
		// it is safe here, and this covers the tail firing `closePortedMenu` outside the recorder.
		deluge::gui::ui::setPadForwardPerform(nullptr);
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest(*fx.song)).to_be_true();
		expect(shellHostUI.shell().stack().depth()).to_be_greater_than(0u);

		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		(void)deluge::gui::ui::forwardGridPad(3, 4, 64);
		expect(shellHostUI.shell().stack().depth()).to_equal(0u);
	});

	// ---- the note modes (`SoundEditor::padAction` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`)
	// ----------------------------------------------

	it("under the note editor over the clip view, sends a main-grid pad to its edit action and a "
	   "sidebar pad to Exit", _ {
		// The edit action's own `if (on)` is the handler's (`InstrumentClipView::handleNoteEditorEditPadAction`). The
		// arm itself, in `SoundEditor::padAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), tests neither
		// `on` nor `currentUIMode`, and its exit is unconditional -- so a release and a UI mode take the same arms,
		// which is what separates this Exit from the tail's (the `currentUIMode == UI_MODE_NONE && on` arm), and the
		// audition column is the note arm's, not the `x == kDisplayWidth + 1` arm's.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteEditor)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::NoteEditorEdit);
		expect(seen).to_equal(PadForwardTarget::NoteEditorEdit);
		expect(seen_x).to_equal(3);
		expect(seen_y).to_equal(4);
		expect(seen_v).to_equal(64);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth - 1, 7, 0)).to_equal(PadForwardTarget::NoteEditorEdit);

		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 64)).to_equal(PadForwardTarget::Exit);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64)).to_equal(PadForwardTarget::Exit);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 0)).to_equal(PadForwardTarget::Exit);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 2, 64)).to_equal(PadForwardTarget::Exit);
		currentUIMode = UI_MODE_NONE;
	});

	it("under the note-row editor over the clip view, sends every pad to the note-row editor", _ {
		// the `inNoteRowEditor` arm of `SoundEditor::padAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): no
		// coordinate test, no `on` test, no mode test -- the handler decides, and the note-row editor normally runs
		// with a row auditioned (`UI_MODE_AUDITIONING`).
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteRowEditor)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::NoteRowEditor);
		expect(seen_x).to_equal(3);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 4, 64)).to_equal(PadForwardTarget::NoteRowEditor);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64)).to_equal(PadForwardTarget::NoteRowEditor);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 0)).to_equal(PadForwardTarget::NoteRowEditor);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::NoteRowEditor);
		currentUIMode = UI_MODE_NONE;
	});

	it("takes the note arms only while the menu is on the guest's stack", _ {
		// Control for the two examples above, varying one thing: the same menu, same root and same
		// root UI, with the menu removed from the stack. `menu_context().root` still reads
		// `NoteEditor`; the arms must not.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteEditor)).to_be_true();
		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(menu_scene().menu_context().root).to_equal(MenuContext::Root::NoteEditor);
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Exit); // the tail
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64)).to_equal(PadForwardTarget::AuditionColumn);
	});

	it("keeps the note arms inside the clip view's case: another root takes its own arm", _ {
		// `SoundEditor::potentialShortcutPadAction`'s `ignoreAction` gate comes before the note arms, and its tail is a separate root: the note arms are nested in
		// `rootUI == &instrumentClipView`, so a note root over another root view changes nothing.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, MenuContext::Root::NoteRowEditor)).to_be_true();
		currentUIMode = UI_MODE_NONE;

		setRootUILowLevel(&keyboardScreen);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Keyboard);
		setRootUILowLevel(&performanceView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Performance);
		setRootUILowLevel(&sessionView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("the real performer hands a NoteEditorEdit pad to the note editor's edit action and leaves the "
	   "menu open", _ {
		// `SoundEditor::padAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) returns `DEALT_WITH` -- no exit.
		// The sidebar half is its `exitCompletely()`, which is the `Exit` arm's `closePortedMenu()`.
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
		// (`InstrumentClipView::handleNoteRowEditorPadAction` in `instrument_clip_view.cpp`) -- but the false arm is
		// main's, kept rather than folded away: the `inNoteRowEditor` arm of `SoundEditor::padAction`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), `bool handled = ...;` then
		// `if (!handled) { exitCompletely(); }`.
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
		// main's `SoundEditor::handleAutomationViewPadAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`):
		// `(x >= kDisplayWidth) || editingParamInAutomationView` -- the sidebar half needs no param agreement at all.
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
		// main's `SoundEditor::handleAutomationViewPadAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)
		// returns NOT_DEALT_WITH, and the automation arm of `SoundEditor::padAction` then does not return -- so main
		// reaches the tail.
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

	it("sends a main-grid pad to automation view from a horizontal page whose focused column is the view's param", _ {
		// `SoundEditor::isEditingAutomationViewParam` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) asks
		// `getCurrentMenuItem()->getParamKind()`, which on a horizontal page answers for its focused column
		// (`Submenu::getParamKind`).
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::PATCHED;
		fx.clip->lastSelectedParamID = params::LOCAL_LPF_FREQ;
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		expect(menu_scene().focused()).to_equal(&nodes::filters_group);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Automation);
	});

	it("sends a main-grid pad to automation view from the page of the cable the view shows", _ {
		// A cable's param kind is `PATCH_CABLE` and its id the cable's descriptor word
		// (`PatchCableStrength::getModelStack` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`)), as
		// the automation view records a selected cable.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
		fx.clip->lastSelectedParamID = kVibratoCableId;
		expect(open_shortcut_over_automation(fx.clip, 6, 2)).to_be_true();
		expect(menu_scene().focused_value()).to_equal(&nodes::vibrato);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Automation);
	});

	it("falls through to the tail from a cable page when the view shows another cable", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
		fx.clip->lastSelectedParamID = static_cast<int32_t>(
		    deluge::synth::CableTarget::depth_of(params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_2).word);
		expect(open_shortcut_over_automation(fx.clip, 6, 2)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("leaves Shift + (0,6) and (0,7) to the automation view when it is the root", _ {
		// `SoundEditor::potentialShortcutPadAction` exempts the interpolation and pad-selection pads from the
		// shortcuts when the root is the automation view; on the view's param they reach `padAction`
		// (`AutomationView::shortcutPadAction` toggles them).
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::PATCHED;
		fx.clip->lastSelectedParamID = params::LOCAL_LPF_FREQ;
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		ShiftHeld shift;
		(void)shellHostUI.padAction(0, 6, 64);
		expect(seen).to_equal(PadForwardTarget::Automation);
		expect(menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		seen = PadForwardTarget::None;
		(void)shellHostUI.padAction(0, 7, 64);
		expect(seen).to_equal(PadForwardTarget::Automation);
	});

	it("closes the menu on Shift + (0,6) over the automation view off its param", _ {
		// Legacy's own outcome: the pad is no shortcut there, and `padAction`'s tail closes the menu.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
		fx.clip->lastSelectedParamID = params::UNPATCHED_BASS;
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		ShiftHeld shift;
		(void)shellHostUI.padAction(0, 6, 64);
		expect(seen).to_equal(PadForwardTarget::Exit);
	});

	it("still claims Shift + (0,6) as a shortcut over any other root", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		ShiftHeld shift;
		(void)shellHostUI.padAction(0, 6, 64);
		expect(seen).to_equal(PadForwardTarget::None);
	});

	it("does not dereference a null clip", _ {
		// The port guards a null `getCurrentClip()`; main's `SoundEditor::isEditingAutomationViewParam`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) dereferences it unguarded and is left as it is.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.song->setCurrentClip(nullptr); // no current clip -- `getCurrentClip()` returns nullptr
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("takes SAVE under the menu as the sound editor's arm: Shift deletes the picker's range, a plain press saves", _ {
		// `SoundEditor::buttonAction`'s SAVE arm (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), through the
		// guest's `forwardMenuSaveButton`: only with no UI mode up and an instrument clip's view at the root.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(fx.instrument);
		deluge::edits::settingFor(*sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		for (int32_t i = 0; i < 3; i++) {
			(void)sound->sourceResources[0].ranges.insertMultiRange(i);
		}
		sound->sourceResources[0].ranges.getElement(0)->topNote = 40;
		sound->sourceResources[0].ranges.getElement(1)->topNote = 60;
		sound->sourceResources[0].ranges.getElement(2)->topNote = 32767;
		deluge::edits::recordRanges(*sound, 0);
		// BROWSE, OSC1's file selector: range-dependent, so on a source of several ranges the picker opens first.
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 0, 5)).to_be_true();
		expect(menu_scene().focused()).to_equal(&nodes::multi_range);

		currentUIMode = UI_MODE_AUDITIONING; // under a UI mode SAVE does nothing, and is still the menu's
		{
			ShiftHeld shift;
			expect(shellHostUI.buttonAction(deluge::hid::button::SAVE, true)).to_equal(ActionResult::DEALT_WITH);
		}
		expect(static_cast<int32_t>(std::ssize(sound->sourceResources[0].ranges))).to_equal(3);
		currentUIMode = UI_MODE_NONE;
		{
			ShiftHeld shift;
			expect(shellHostUI.buttonAction(deluge::hid::button::SAVE, true)).to_equal(ActionResult::DEALT_WITH);
		}
		expect(static_cast<int32_t>(std::ssize(sound->sourceResources[0].ranges))).to_equal(2);
		display->cancelPopup();

		(void)shellHostUI.buttonAction(deluge::hid::button::SAVE, true);
		expect(getCurrentUI()).to_equal(&saveInstrumentPresetUI);
	});

	it("swaps the clip view and the keyboard screen under the menu on KEYBOARD, and lights KEYBOARD", _ {
		// `SoundEditor::buttonAction`'s KEYBOARD arm: on a press with no UI mode, over an instrument clip's view.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		fx.clip->onAutomationClipView = false;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		expect(shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, true)).to_equal(ActionResult::DEALT_WITH);
		expect(getRootUI()).to_equal(&keyboardScreen);
		expect(led_on(IndicatorLED::KEYBOARD)).to_be_true();
		expect(menu_scene().menu_context().root_is_keyboard).to_be_true(); // the root facts were re-read
		(void)shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, false); // the release changes nothing
		expect(getRootUI()).to_equal(&keyboardScreen);
		expect(shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, true)).to_equal(ActionResult::DEALT_WITH);
		expect(getRootUI()).to_equal(&instrumentClipView);
		expect(led_on(IndicatorLED::KEYBOARD)).to_be_false();
		expect(shellHostUI.shell().stack().depth()).to_be_greater_than(0u); // the menu stays open throughout
	});

	it("takes KEYBOARD from the keyboard screen back to the automation view when the clip was on it", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		fx.clip->onAutomationClipView = true;
		expect(open_shortcut_over(&keyboardScreen, fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		(void)shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, true);
		expect(getRootUI()).to_equal(&automationView);
		fx.clip->onAutomationClipView = false;
	});

	it("swallows KEYBOARD under the menu with a UI mode up, and leaves the root", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		expect(shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, true)).to_equal(ActionResult::DEALT_WITH);
		expect(getRootUI()).to_equal(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
	});

	it("leaves the menu for the session view on SESSION, or for the arranger when a clip instance was entered", _ {
		// `SoundEditor::buttonAction`'s SESSION_VIEW arm: `exitCompletely()`, then `changeRootUI`.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		fx.song->persistentView.lastClipInstanceEnteredStartPos = -1;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		const bool open = openUI(&shellHostUI) || isUIOpen(&shellHostUI);
		expect(open).to_be_true("shellHostUI neither opened nor was open");
		currentUIMode = UI_MODE_NONE;
		(void)shellHostUI.buttonAction(deluge::hid::button::SESSION_VIEW, true);
		expect(getRootUI()).to_equal(&sessionView);
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(shellHostUI.shell().stack().depth()).to_equal(0u); // closed now, not at the next tick

		GlobalGuestGuard::empty_guest_stack();
		fx.song->persistentView.lastClipInstanceEnteredStartPos = 0;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::SESSION_VIEW, true);
		expect(getRootUI()).to_equal(&arrangerView);
		fx.song->persistentView.lastClipInstanceEnteredStartPos = -1;
	});

	it("refuses SESSION with a UI mode up, except the note-row editor's own audition", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		fx.song->persistentView.lastClipInstanceEnteredStartPos = -1;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.buttonAction(deluge::hid::button::SESSION_VIEW, true);
		expect(getRootUI()).to_equal(&instrumentClipView);
		expect(isUIOpen(&shellHostUI)).to_be_true();

		GlobalGuestGuard::empty_guest_stack();
		nullifyUIs();
		expect(spec::open_note_mode_over_clip_view(fx, MenuContext::Root::NoteRowEditor)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING; // the row the editor holds, and nothing else
		(void)shellHostUI.buttonAction(deluge::hid::button::SESSION_VIEW, true);
		expect(getRootUI()).to_equal(&sessionView);
		// `exitCompletely` exits the note-row editor before its close, which ends the row's audition: the guest's own
		// close (`closeNow`), not a bare removal of the menu.
		expect(isUIModeActive(UI_MODE_AUDITIONING)).to_be_false();
		currentUIMode = UI_MODE_NONE;
	});

	it("holds fill on SYNC-SCALING under the menu when its action is Fill, on the press and the release", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction);
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		expect(deluge::gui::ui::forwardMenuViewButton(deluge::hid::button::SYNC_SCALING, true).has_value()).to_be_false();
		expect(fx.song->runtimeView.fillModeActive).to_be_false();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::Fill);
		expect(shellHostUI.buttonAction(deluge::hid::button::SYNC_SCALING, true)).to_equal(ActionResult::DEALT_WITH);
		expect(fx.song->runtimeView.fillModeActive).to_be_true();
		expect(led_on(IndicatorLED::SYNC_SCALING)).to_be_true();
		expect(shellHostUI.buttonAction(deluge::hid::button::SYNC_SCALING, false)).to_equal(ActionResult::DEALT_WITH);
		expect(fx.song->runtimeView.fillModeActive).to_be_false();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction, saved);
	});

	it("leaves KEYBOARD, SESSION and SYNC-SCALING to a scene over the menu", _ {
		// Legacy's arms are `SoundEditor`'s, reached only while it is the current UI: under a browser it opened they
		// are the browser's. With nothing taking input but the menu, `menuTakesInput` is what is false here.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardMenuViewButton(deluge::hid::button::KEYBOARD, true).has_value()).to_be_false();
		expect(deluge::gui::ui::forwardMenuViewButton(deluge::hid::button::SESSION_VIEW, true).has_value()).to_be_false();
		expect(getRootUI()).to_equal(&instrumentClipView);
	});

	it("opens the shortcut gate on shift or on an audition over the clip view, and swallows outside its two modes", _ {
		// `potentialShortcutPadAction`: without shift, only `isUIModeActive(UI_MODE_AUDITIONING)` over the instrument
		// clip view; past that, only within {AUDITIONING, HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR}, else `DEALT_WITH`.
		using deluge::gui::ui::ShortcutPadGate;
		using deluge::gui::ui::shortcutPadGate;
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(shortcutPadGate(false)).to_equal(ShortcutPadGate::Forward);
		expect(shortcutPadGate(true)).to_equal(ShortcutPadGate::Shift);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(shortcutPadGate(false)).to_equal(ShortcutPadGate::Audition);
		setRootUILowLevel(&keyboardScreen);
		expect(shortcutPadGate(false)).to_equal(ShortcutPadGate::Forward);
		setRootUILowLevel(&automationView);
		expect(shortcutPadGate(false)).to_equal(ShortcutPadGate::Forward);
		currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
		expect(shortcutPadGate(true)).to_equal(ShortcutPadGate::Shift);
		currentUIMode = UI_MODE_NOTES_PRESSED;
		expect(shortcutPadGate(true)).to_equal(ShortcutPadGate::Swallow);
		currentUIMode = UI_MODE_NONE;
	});

	it("claims an audition-held main-grid press over the clip view, and the menu jumps on it", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 2, 0)).to_be_true(); // OSC1 level: any page but the filters
		shellHostUI.shell().tick(1);
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.padAction(8, 7, 64);
		(void)shellHostUI.padAction(8, 7, 0);
		shellHostUI.shell().tick(2);
		expect(seen).to_equal(PadForwardTarget::None); // not forwarded
		expect(menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		currentUIMode = UI_MODE_NONE;
	});

	it("swallows a shift-held main-grid press under the menu while notes are pressed", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 2, 0)).to_be_true();
		shellHostUI.shell().tick(1);
		const Node* before = menu_scene().focused_value();
		currentUIMode = UI_MODE_NOTES_PRESSED;
		ShiftHeld shift;
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true); // the guest's shell holds SHIFT too
		(void)shellHostUI.padAction(8, 7, 64);
		shellHostUI.shell().tick(2);
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		expect(seen).to_equal(PadForwardTarget::None);
		expect(menu_scene().focused_value()).to_equal(before);
		currentUIMode = UI_MODE_NONE;
	});

	it("opens the note-row editor on PLAY DIRECTION from the open menu on audition + the sequence-direction pad", _ {
		// `potentialShortcutPadAction`'s special case: an instrument clip, OLED, HorizontalMenus on, one row auditioning.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		shellHostUI.shell().tick(1);
		deluge::gui::ui::setPadForwardPerform(nullptr); // the audition is the clip view's, for real
		(void)shellHostUI.padAction(kDisplayWidth + 1, 3, 64);
		expect(isUIModeActive(UI_MODE_AUDITIONING)).to_be_true();
		expect(instrumentClipView.getNumNoteRowsAuditioning()).to_equal(1);
		(void)shellHostUI.padAction(4, 7, 64);
		shellHostUI.shell().tick(2);
		shellHostUI.shell().tick(3);
		expect(menu_scene().menu_context().root).to_equal(MenuContext::Root::NoteRowEditor);
		expect(menu_scene().focused_value()).to_equal(&nodes::sequence_direction);
		expect(shellHostUI.shell().stack().depth()).to_equal(1u); // re-rooted, not stacked twice
		expect(numUIsOpen).to_equal(2);
		(void)shellHostUI.padAction(4, 7, 0);
		(void)shellHostUI.padAction(kDisplayWidth + 1, 3, 0);
	});

	it("jumps rather than entering the note-row editor when two rows audition", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		shellHostUI.shell().tick(1);
		deluge::gui::ui::setPadForwardPerform(nullptr);
		(void)shellHostUI.padAction(kDisplayWidth + 1, 3, 64);
		(void)shellHostUI.padAction(kDisplayWidth + 1, 4, 64);
		(void)shellHostUI.padAction(4, 7, 64);
		shellHostUI.shell().tick(2);
		shellHostUI.shell().tick(3);
		expect(menu_scene().menu_context().root).to_equal(MenuContext::Root::Sound);
		expect(menu_scene().focused_value()).to_equal(&nodes::sequence_direction);
		(void)shellHostUI.padAction(4, 7, 0);
		(void)shellHostUI.padAction(kDisplayWidth + 1, 3, 0);
		(void)shellHostUI.padAction(kDisplayWidth + 1, 4, 0);
	});

	it("keeps the kit's selected drum while the menu scene is current, as while the sound editor is", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_kit_song();
		auto* a = spec::add_drum<SoundDrum>(fx);
		auto* b = spec::add_drum<SoundDrum>(fx);
		auto* kit = static_cast<Kit*>(fx.instrument);
		kit->selectedDrum = a;
		setRootUILowLevel(&instrumentClipView);
		instrumentClipView.setSelectedDrum(b, false); // no menu: the selection moves
		expect(kit->selectedDrum).to_equal(b);
		kit->selectedDrum = a;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		expect(deluge::gui::ui::menuCurrent()).to_be_true();
		instrumentClipView.setSelectedDrum(b, false);
		expect(kit->selectedDrum).to_equal(a);
	});

	it("freezes the selection under exactly the scenes that stand in for main's three UIs", _ {
		// `setSelectedDrum`'s `currentUI != &sampleBrowser && != &sampleMarkerEditor && != &renameDrumUI`.
		namespace browser = deluge::ui::scenes::browser;
		namespace rename = deluge::ui::scenes::rename;
		const auto id = [](auto e) { return static_cast<uint16_t>(e); };
		const auto drumSelectionFrozenBy = [](SceneKind kind, uint16_t which) {
			return deluge::gui::ui::drumSelectionFrozenBy(static_cast<uint8_t>(kind), which);
		};
		expect(drumSelectionFrozenBy(browser::kBrowserSceneKind, id(browser::BrowserId::Sample))).to_be_true();
		expect(drumSelectionFrozenBy(browser::kBrowserSceneKind, id(browser::BrowserId::DxSyx))).to_be_false();
		expect(drumSelectionFrozenBy(browser::kBrowserSceneKind, id(browser::BrowserId::MidiDeviceDefinition)))
		    .to_be_false();
		expect(drumSelectionFrozenBy(deluge::ui::scenes::marker::kMarkerSceneKind, 0)).to_be_true();
		expect(drumSelectionFrozenBy(rename::kRenameSceneKind, id(rename::TargetId::Drum))).to_be_true();
		expect(drumSelectionFrozenBy(rename::kRenameSceneKind, id(rename::TargetId::Clip))).to_be_false();
		expect(drumSelectionFrozenBy(rename::kRenameSceneKind, id(rename::TargetId::Output))).to_be_false();
		expect(drumSelectionFrozenBy(deluge::ui::scenes::slicing::kSlicerSceneKind, 0)).to_be_false();
		expect(drumSelectionFrozenBy(deluge::ui::scenes::recorder::kRecorderSceneKind, 0)).to_be_false();
		expect(drumSelectionFrozenBy(deluge::ui::shell::SceneKind::FirstApp, 0)).to_be_false();
	});

	it("keeps the kit's selected drum while the drum rename scene is over the menu, and not under a clip rename", _ {
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_kit_song();
		auto* a = spec::add_drum<SoundDrum>(fx);
		auto* b = spec::add_drum<SoundDrum>(fx);
		auto* kit = static_cast<Kit*>(fx.instrument);
		fx.clip->affectEntire = false;
		kit->selectedDrum = a;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		namespace rename = deluge::ui::scenes::rename;
		rename::rename_scene().open(menu_scene().menu_context());
		expect(shellHostUI.shell().stack().push(
		           SceneId{rename::kRenameSceneKind, static_cast<uint16_t>(rename::TargetId::Drum)}))
		    .to_be_true();
		expect(deluge::gui::ui::menuCurrent()).to_be_false();
		instrumentClipView.setSelectedDrum(b, false);
		expect(kit->selectedDrum).to_equal(a);

		shellHostUI.shell().stack().pop(deluge::ui::shell::SceneKind::Null);
		rename::rename_scene().open(menu_scene().menu_context());
		expect(shellHostUI.shell().stack().push(
		           SceneId{rename::kRenameSceneKind, static_cast<uint16_t>(rename::TargetId::Clip)}))
		    .to_be_true();
		instrumentClipView.setSelectedDrum(b, false);
		expect(kit->selectedDrum).to_equal(b);
	});

	it("on a kit row's item that uses it, AFFECT ENTIRE holds; elsewhere it toggles the clip's affect-entire", _ {
		// `SoundEditor::buttonAction`'s AFFECT_ENTIRE arm: the hold mode on `editingKitRow() && usesAffectEntire()`,
		// otherwise `instrumentClipView.InstrumentClipMinder::buttonAction`.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_kit_song();
		auto* a = spec::add_drum<SoundDrum>(fx);
		(void)spec::add_drum<SoundDrum>(fx);
		static_cast<Kit*>(fx.instrument)->selectedDrum = a;
		fx.clip->affectEntire = false;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true(); // the row's FILTER page
		shellHostUI.shell().tick(1);
		currentUIMode = UI_MODE_NONE;
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, true);
		expect(currentUIMode).to_equal(uint32_t(UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR));
		expect(menu::bindings::writes_whole_kit(menu_scene().menu_context())).to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, false);
		expect(currentUIMode).to_equal(uint32_t(UI_MODE_NONE));
		expect(menu::bindings::writes_whole_kit(menu_scene().menu_context())).to_be_false();
		// The hold starts only with no UI mode up (`currentUIMode == UI_MODE_NONE`).
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, true);
		expect(menu::bindings::writes_whole_kit(menu_scene().menu_context())).to_be_false();
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, false);
		currentUIMode = UI_MODE_NONE;

		GlobalGuestGuard::empty_guest_stack();
		nullifyUIs();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 2, 0)).to_be_true(); // the OSC page; walk up to the root
		menu_scene().open(nodes::root_drum, menu_scene().menu_context());
		shellHostUI.shell().tick(2);
		expect(menu_scene().takes_affect_entire()).to_be_false();
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, true);
		expect(fx.clip->affectEntire).to_be_true();
		expect(menu_scene().menu_context().affect_entire).to_be_true();
		expect(currentUIMode).to_equal(uint32_t(UI_MODE_NONE));
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, false);
		(void)shellHostUI.buttonAction(deluge::hid::button::AFFECT_ENTIRE, true);
		expect(fx.clip->affectEntire).to_be_false();
	});

	it("re-renders the clip view's sidebar on every screen change, as beginScreen does", _ {
		// `SoundEditor::beginScreen` -> `possibleChangeToCurrentRangeDisplay`: the sidebar redraws on each new screen,
		// which is when a kit's selected-drum pad goes dark under affect-entire (`drawAuditionSquare`).
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 2, 0)).to_be_true();
		shellHostUI.shell().tick(1);
		deluge::gui::ui::refreshShellRangeDisplay();
		whichSideRowsNeedRendering = 0;
		deluge::gui::ui::refreshShellRangeDisplay(); // the same screen: nothing
		expect(whichSideRowsNeedRendering).to_equal(0u);
		menu_scene().open(nodes::sound_root, menu_scene().menu_context());
		shellHostUI.shell().tick(2);
		deluge::gui::ui::refreshShellRangeDisplay(); // a new screen: the sidebar
		expect(whichSideRowsNeedRendering).not_().to_equal(0);
	});

	it("selects the played note's range under the picker, highlights the range's notes, and re-renders the root", _ {
		// `InstrumentClipView::potentiallyUpdateMultiRangeMenu` -> `noteOnToChangeRange`, `drawAuditionSquare`'s
		// selecting-ranges test (`isUntransposedNoteWithinRange`), and `possibleChangeToCurrentRangeDisplay`, each
		// asking the guest when the menu is up.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(fx.instrument);
		deluge::edits::settingFor(*sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		for (int32_t i = 0; i < 3; i++) {
			(void)sound->sourceResources[0].ranges.insertMultiRange(i);
		}
		const int32_t row0 = fx.clip->getYNoteFromYDisplay(0, fx.song) + sound->getTranspose();
		// Row 0's note and the one above it; two more; the rest (the clip's rows step through its scale).
		sound->sourceResources[0].ranges.getElement(0)->topNote = static_cast<int16_t>(row0 + 1);
		sound->sourceResources[0].ranges.getElement(1)->topNote = static_cast<int16_t>(row0 + 3);
		sound->sourceResources[0].ranges.getElement(2)->topNote = 32767;
		deluge::edits::recordRanges(*sound, 0);
		expect(deluge::gui::ui::rangeDependentMenuOpen()).to_be_false(); // no menu: legacy's own answer
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 0, 5)).to_be_true();
		expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		shellHostUI.shell().tick(1);
		expect(deluge::gui::ui::rangePickerOpen()).to_be_true();
		expect(deluge::gui::ui::rangeDependentMenuOpen()).to_be_true();
		expect(static_cast<int>(menu_scene().menu_context().range_index)).to_equal(1);
		currentUIMode = UI_MODE_NONE;
		whichSideRowsNeedRendering = 0;
		deluge::gui::ui::refreshShellRangeDisplay(); // the range is new to the root: a sidebar render
		expect(whichSideRowsNeedRendering).not_().to_equal(0);
		whichSideRowsNeedRendering = 0;
		deluge::gui::ui::refreshShellRangeDisplay(); // unchanged: none
		expect(whichSideRowsNeedRendering).to_equal(0u);
		{
			// The same range, on the oscillator's page, which is not range-dependent: the highlight goes.
			const deluge::ui::scenes::menu::MenuContext ctx = menu_scene().menu_context();
			menu_scene().open(nodes::source0, ctx);
			shellHostUI.shell().tick(1);
			expect(deluge::gui::ui::rangeDependentMenuOpen()).to_be_false();
			deluge::gui::ui::refreshShellRangeDisplay();
			expect(whichSideRowsNeedRendering).not_().to_equal(0);
			whichSideRowsNeedRendering = 0;
			menu_scene().open(nodes::multi_range, ctx);
			shellHostUI.shell().tick(1);
			deluge::gui::ui::refreshShellRangeDisplay(); // and comes back
			expect(whichSideRowsNeedRendering).not_().to_equal(0);
			// `MenuItem::isRangeDependent` (`origin/main:src/deluge/gui/menu_item/menu_item.h`) is true for the
			// transposes and the loop points only: the file selector and the recorder check the range on open but do
			// not highlight it.
			for (const Node* item : {&nodes::file0_selector, &nodes::sample0_recorder}) {
				menu_scene().open(*item, ctx);
				shellHostUI.shell().tick(1);
				expect(deluge::gui::ui::rangeDependentMenuOpen()).to_be_false();
			}
			for (const Node* item : {&nodes::source0_transpose, &nodes::sample0_start, &nodes::sample0_end}) {
				menu_scene().open(*item, ctx);
				shellHostUI.shell().tick(1);
				expect(deluge::gui::ui::rangeDependentMenuOpen()).to_be_true();
			}
			menu_scene().open(nodes::multi_range, ctx);
			shellHostUI.shell().tick(1);
			deluge::gui::ui::refreshShellRangeDisplay();
			whichSideRowsNeedRendering = 0;
		}
		{
			// A range deleted under the same index: the root re-renders, as `deletePress` has it do.
			whichSideRowsNeedRendering = 0;
			deluge::gui::ui::refreshShellRangeDisplay();
			expect(whichSideRowsNeedRendering).to_equal(0u);
			const int32_t before = menu_scene().menu_context().range_index;
			sound->deleteMultiRange(0, 2);
			deluge::edits::recordRanges(*sound, 0);
			expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(before);
			deluge::gui::ui::refreshShellRangeDisplay();
			expect(whichSideRowsNeedRendering).not_().to_equal(0);
			(void)sound->sourceResources[0].ranges.insertMultiRange(2);
			sound->sourceResources[0].ranges.getElement(1)->topNote = static_cast<int16_t>(row0 + 3);
			sound->sourceResources[0].ranges.getElement(2)->topNote = 32767;
			deluge::edits::recordRanges(*sound, 0);
			deluge::gui::ui::refreshShellRangeDisplay();
			whichSideRowsNeedRendering = 0;
		}
		{
			// An edge moved under the same range and count: `MultiRange::selectEncoderAction`'s edge arm re-renders the
			// keyboard only, so the clip view's sidebar keeps its highlight.
			const int32_t top = sound->sourceResources[0].ranges.getElement(1)->topNote;
			sound->sourceResources[0].ranges.getElement(1)->topNote = static_cast<int16_t>(top + 1);
			deluge::edits::recordRanges(*sound, 0);
			deluge::gui::ui::refreshShellRangeDisplay();
			expect(whichSideRowsNeedRendering).to_equal(0u);
			sound->sourceResources[0].ranges.getElement(1)->topNote = static_cast<int16_t>(top);
			deluge::edits::recordRanges(*sound, 0);
			deluge::gui::ui::refreshShellRangeDisplay();
			whichSideRowsNeedRendering = 0;
		}

		// This spec records the forward rather than performing it (`seen`); what it performs is the clip view's pad.
		(void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Shell);
		(void)instrumentClipView.padAction(kDisplayWidth + 1, 5, 64); // an audition note in range 2
		// The note arrives between the guest's ticks: the picker asks for a frame of its own.
		expect(deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Shell).has(deluge::base::notify::Dirty::Oled))
		    .to_be_true();
		(void)instrumentClipView.padAction(kDisplayWidth + 1, 5, 0);
		shellHostUI.shell().tick(2);
		expect(static_cast<int>(menu_scene().menu_context().range_index)).to_equal(2);
		{
			// `drawAuditionSquare`'s selecting-ranges arm: +30 on the audition squares whose notes are in the range.
			RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
			uint8_t occupancy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
			(void)instrumentClipView.renderSidebar(0xFF, image, occupancy);
			expect(image[3][kDisplayWidth + 1]).to_equal(RGB{30, 30, 30}); // in range 2
			expect(image[1][kDisplayWidth + 1]).to_equal(RGB{0, 0, 0});    // in range 1
		}
		expect(deluge::gui::ui::noteWithinCurrentRange(fx.clip->getYNoteFromYDisplay(6, fx.song))).to_be_true();
		expect(deluge::gui::ui::noteWithinCurrentRange(fx.clip->getYNoteFromYDisplay(1, fx.song))).to_be_false();

		(void)instrumentClipView.padAction(kDisplayWidth + 1, 0, 64); // and one in range 0
		(void)instrumentClipView.padAction(kDisplayWidth + 1, 0, 0);
		shellHostUI.shell().tick(3);
		expect(static_cast<int>(menu_scene().menu_context().range_index)).to_equal(0);
		expect(deluge::gui::ui::noteWithinCurrentRange(fx.clip->getYNoteFromYDisplay(0, fx.song))).to_be_true();
		expect(deluge::gui::ui::noteWithinCurrentRange(fx.clip->getYNoteFromYDisplay(1, fx.song))).to_be_false();

		// `scrollVertical_potentiallySwitchOnAuditionedNotes`: scrolling with row 0 held sounds its new note, which moves
		// the picker too.
		(void)instrumentClipView.padAction(kDisplayWidth + 1, 0, 64);
		(void)instrumentClipView.verticalEncoderAction(4);
		shellHostUI.shell().tick(4);
		expect(static_cast<int>(menu_scene().menu_context().range_index)).to_equal(2);
		(void)instrumentClipView.padAction(kDisplayWidth + 1, 0, 0);
	});

	it("selects a keyboard note's range under the picker and highlights the range on the isomorphic keyboard", _ {
		// `KeyboardScreen::updateActiveNotes`' `noteOnToChangeRange` arm and `KeyboardLayoutIsomorphic::renderPads`'
		// +35, each asking the guest when the menu is up over the keyboard.
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(fx.instrument);
		deluge::edits::settingFor(*sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		for (int32_t i = 0; i < 2; i++) {
			(void)sound->sourceResources[0].ranges.insertMultiRange(i);
		}
		sound->sourceResources[0].ranges.getElement(0)->topNote = 59; // below middle C
		sound->sourceResources[0].ranges.getElement(1)->topNote = 32767;
		deluge::edits::recordRanges(*sound, 0);
		fx.clip->onKeyboardScreen = true;
		// The pads with no menu up: nothing highlighted, so every lit difference later is the +35.
		RGB plain[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		uint8_t occupancy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		setRootUILowLevel(&keyboardScreen);
		(void)keyboardScreen.renderMainPads(0xFF, plain, occupancy);
		expect(open_shortcut_over(&keyboardScreen, fx.clip, 0, 5)).to_be_true();
		expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		shellHostUI.shell().tick(1);
		currentUIMode = UI_MODE_NONE;
		const int32_t start = static_cast<int32_t>(menu_scene().menu_context().range_index);

		// The pad whose note is lowest on the keyboard, then its range's other half.
		int32_t pressed = -1;
		for (int32_t y = 0; y < kDisplayHeight && pressed < 0; y++) {
			for (int32_t x = 0; x < kDisplayWidth && pressed < 0; x++) {
				(void)keyboardScreen.padAction(x, y, 64);
				(void)keyboardScreen.padAction(x, y, 0);
				shellHostUI.shell().tick(static_cast<uint32_t>(2 + y * kDisplayWidth + x));
				if (static_cast<int32_t>(menu_scene().menu_context().range_index) != start) {
					pressed = y * kDisplayWidth + x;
				}
			}
		}
		to_be_at_least(expect(pressed), 0); // some pad's note fell in the other range, and moved the picker
		const int32_t now = static_cast<int32_t>(menu_scene().menu_context().range_index);

		RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		(void)keyboardScreen.renderMainPads(0xFF, image, occupancy);
		int32_t brighter = 0;
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				brighter += image[y][x].r > plain[y][x].r ? 1 : 0;
			}
		}
		expect(brighter).to_be_greater_than(0);
		expect(now).not_().to_equal(start);

		// An edge moved: `MultiRange::selectEncoderAction`'s edge arm re-renders the keyboard.
		deluge::gui::ui::refreshShellRangeDisplay();
		whichMainRowsNeedRendering = 0;
		sound->sourceResources[0].ranges.getElement(0)->topNote = 60;
		deluge::edits::recordRanges(*sound, 0);
		deluge::gui::ui::refreshShellRangeDisplay();
		expect(whichMainRowsNeedRendering).not_().to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(pad_forward_spec, pad_forward)
