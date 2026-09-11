/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The note mode's routing through the guest. See `shell_note_mode.h` for the seam and why it
///        lives here.

#include "gui/ui/shell_note_mode.h"

#include "gui/ui/shell_action_dispatch.h" // closePortedMenu()
#include "gui/ui/shell_host_ui.h"         // shellHostUI
#include "gui/ui/sound_editor.h"          // soundEditor
#include "gui/ui/ui.h"                    // uiNeedsRendering
#include "gui/views/instrument_clip_view.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/note_editor_open.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/note.h"
#include "scenes/registry.h"
#include "shell/scene.h"

namespace deluge::gui::ui {
namespace {

using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace nodes = deluge::scenes::menu::nodes;

// The real calls, one per `instrumentClipView` member `SoundEditor` names for the mode.

ActionResult realNoteEditorButton(deluge::hid::Button b, bool on, bool inCardRoutine) {
	return instrumentClipView.handleNoteEditorButtonAction(b, on, inCardRoutine);
}

ActionResult realNoteRowEditorButton(deluge::hid::Button b, bool on, bool inCardRoutine) {
	return instrumentClipView.handleNoteRowEditorButtonAction(b, on, inCardRoutine);
}

ActionResult realNoteEditorHorizontal(int32_t offset) {
	return instrumentClipView.handleNoteEditorHorizontalEncoderAction(offset);
}

ActionResult realNoteRowEditorHorizontal(int32_t offset) {
	return instrumentClipView.handleNoteRowEditorHorizontalEncoderAction(offset);
}

ActionResult realNoteEditorVertical(int32_t offset, bool inCardRoutine) {
	return instrumentClipView.handleNoteEditorVerticalEncoderAction(offset, inCardRoutine);
}

ActionResult realNoteRowEditorVertical(int32_t offset, bool inCardRoutine) {
	return instrumentClipView.handleNoteRowEditorVerticalEncoderAction(offset, inCardRoutine);
}

void realNoteEditorEditPad(int32_t x, int32_t y, int32_t on) {
	instrumentClipView.handleNoteEditorEditPadAction(x, y, on);
}

bool realNoteRowEditorPad(int32_t x, int32_t y, int32_t on) {
	return instrumentClipView.handleNoteRowEditorPadAction(x, y, on);
}

void realExitNoteEditor() {
	instrumentClipView.exitNoteEditor();
}

void realExitNoteRowEditor() {
	instrumentClipView.exitNoteRowEditor();
}

constexpr NoteModeLegacyCalls kRealCalls{
    .noteEditorButton = realNoteEditorButton,
    .noteRowEditorButton = realNoteRowEditorButton,
    .noteEditorHorizontal = realNoteEditorHorizontal,
    .noteRowEditorHorizontal = realNoteRowEditorHorizontal,
    .noteEditorVertical = realNoteEditorVertical,
    .noteRowEditorVertical = realNoteRowEditorVertical,
    .noteEditorEditPad = realNoteEditorEditPad,
    .noteRowEditorPad = realNoteRowEditorPad,
    .exitNoteEditor = realExitNoteEditor,
    .exitNoteRowEditor = realExitNoteRowEditor,
};

const NoteModeLegacyCalls* installed = &kRealCalls;

/// @brief The mode a menu root names -- `inNoteEditor()` / `inNoteRowEditor()`'s comparison of
///        `menuItemNavigationRecord[0]` against the two root items (`gui/ui/sound_editor.cpp:1916-1922`),
///        in the port's representation of "which root is open".
NoteModeKind modeForRoot(MenuContext::Root root) {
	switch (root) {
	case MenuContext::Root::NoteEditor:
		return NoteModeKind::NoteEditor;
	case MenuContext::Root::NoteRowEditor:
		return NoteModeKind::NoteRowEditor;
	default:
		return NoteModeKind::None;
	}
}

} // namespace

NoteModeKind activeNoteMode() {
	// **On the guest's stack, not frontmost.** `menuIsFrontmost` (`gui/ui/shell_guest_services.cpp`)
	// asks `input_top()`, which is the port of `getCurrentUI() == &soundEditor`. The note mode is not
	// that question: `inNoteEditor()` reads the navigation record, not the current UI, so it stays true
	// while a popup or another UI sits over the sound editor. So this walks every stacked scene, and a
	// scene over the menu -- input-taking or not -- leaves the answer alone.
	//
	// Identity against `menu_scene()`, for `menuIsFrontmost`'s own reason: the registry maps several
	// application kinds, and only one object is the menu.
	const deluge::ui::shell::Scene* const menu = &deluge::scenes::menu_scene();
	for (const deluge::ui::shell::Scene* scene : shellHostUI.shell().stack().scenes()) {
		if (scene == menu) {
			// `menu_context()` (`scenes/menu/menu_scene.h:653`) is replaced only by `MenuScene::open`
			// (`scenes/menu/menu_scene.cpp:332-336`) and by a song swap (`:576-579`, which resets the
			// root to `Settings`), so while the menu is stacked this is the root it was opened at.
			return modeForRoot(deluge::scenes::menu_scene().menu_context().root);
		}
	}
	return NoteModeKind::None;
}

std::optional<ActionResult> forwardNoteButton(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	const NoteModeKind mode = activeNoteMode();
	if (mode == NoteModeKind::None) {
		// Neither note arm's test holds (`gui/ui/sound_editor.cpp:617`, `:621`).
		return std::nullopt;
	}

	// **The six buttons legacy's chain takes before it reaches the note arms.** Each is an outer
	// `else if` on the button alone, so it consumes the press whether or not its inner test holds, and
	// none of them is the note handler's. Under the guest each is the ported menu's, fed.
	if (b == SELECT_ENC      // `:385`
	    || b == BACK         // `:456`
	    || b == SAVE         // `:488`
	    || b == LEARN        // `:505`
	    || b == KEYBOARD     // `:564`
	    || b == SESSION_VIEW // `:605`
	) {
		return std::nullopt;
	}

	// **SYNTH, KIT, MIDI and CV: the column focus, which is the ported menu's.** Both handlers send
	// these to `soundEditor.getCurrentMenuItem()->buttonAction(...)`
	// (`gui/views/instrument_clip_view.cpp:3715-3717`, `:3955-3957`), whose target is the horizontal
	// menu's column focus -- design spec §3's, which `MenuScene` carries. Forwarding them would also
	// dereference that navigation record, which no guest path writes.
	if (b == SYNTH || b == KIT || b == MIDI || b == CV) {
		return std::nullopt;
	}

	// **PLAY, SHIFT and the two gold-knob pushes: `NOT_DEALT_WITH`** in both handlers
	// (`:3727-3729`, `:3975-3977`), which is exactly what the guest's fed path already returns -- so
	// feeding them keeps legacy's answer and also keeps the shell's own shift latch fed. (The gold-knob
	// pushes never get here: `ShellHostUI::buttonAction` returns for them first.)
	if (b == PLAY || b == SHIFT || b == MOD_ENCODER_0 || b == MOD_ENCODER_1) {
		return std::nullopt;
	}

	// **Everything else is the handler's, and its answer is the guest's answer.** AFFECT_ENTIRE gets
	// here at all because `SoundEditor`'s own AFFECT_ENTIRE arm is guarded on `!inNoteEditor() &&
	// !inNoteRowEditor()` (`gui/ui/sound_editor.cpp:540`, design spec §4 site 3). In the handlers,
	// X_ENC and SYNC_SCALING reach `InstrumentClipView::buttonAction` through their `return
	// buttonAction(...)` arms (`gui/views/instrument_clip_view.cpp:3721-3723`, `:3961-3963`);
	// AFFECT_ENTIRE toggles a kit clip's `affectEntire` in the note-row editor (`:3965-3971`) and is
	// swallowed in the note editor; every other button, CROSS_SCREEN and SCALE included, is swallowed
	// as `DEALT_WITH` (`:3731`, `:3979`).
	const NoteModeLegacyCalls& calls = noteModeLegacyCalls();
	if (mode == NoteModeKind::NoteEditor) {
		return calls.noteEditorButton(b, on, inCardRoutine); // `sound_editor.cpp:617-619`
	}
	return calls.noteRowEditorButton(b, on, inCardRoutine); // `sound_editor.cpp:621-623`
}

std::optional<ActionResult> forwardNoteHorizontalEncoder(int32_t offset) {
	switch (activeNoteMode()) {
	case NoteModeKind::NoteEditor:
		return noteModeLegacyCalls().noteEditorHorizontal(offset); // `gui/ui/sound_editor.cpp:1047-1049`
	case NoteModeKind::NoteRowEditor:
		return noteModeLegacyCalls().noteRowEditorHorizontal(offset); // `:1050-1052`
	case NoteModeKind::None:
		break;
	}
	return std::nullopt;
}

std::optional<ActionResult> forwardNoteVerticalEncoder(int32_t offset, bool inCardRoutine) {
	switch (activeNoteMode()) {
	case NoteModeKind::NoteEditor:
		return noteModeLegacyCalls().noteEditorVertical(offset, inCardRoutine); // `gui/ui/sound_editor.cpp:1593-1595`
	case NoteModeKind::NoteRowEditor:
		return noteModeLegacyCalls().noteRowEditorVertical(offset, inCardRoutine); // `:1596-1598`
	case NoteModeKind::None:
		break;
	}
	return std::nullopt;
}

void exitNoteModeBeforeClose() {
	// **The root alone, and why that is still this session's root.** Both of `ShellHostUI`'s close
	// paths reach `closeGuest` with the menu already gone from the stack, so `activeNoteMode()` would
	// answer `None` on every real close. `menu_context().root` outlives the stack, so what matters is
	// every writer that could replace it between this session's open and its close. There are three:
	//
	//   * `openShellMenu`, the guest's one open site, runs `menu_scene().open(...)` before
	//     `openUI(&shellHostUI)` (`gui/ui/shell_menu_entry.cpp:206-208`), so every session begins by
	//     writing its own root.
	//   * `GlobalControls::handle_settings_menu` (`scenes/services/global_controls.cpp:307`), the port
	//     of Shift+Select, re-opens the menu at `Root::Settings` (`:337`), and the guest's shell does
	//     install `GlobalControls` (`gui/ui/shell_guest_services.cpp:173-174`). It cannot run during a
	//     guest session today: its arm is reached only when the input-taking scene is a root scene,
	//     `stack_.input_top().is_root()` (`global_controls.cpp:580`). `Scene::is_root` defaults to false
	//     (`shell/scene.h:526`), and neither `MenuScene` nor `LoadingScene` overrides it. The one
	//     override that answers true, `AudioClipScene` (`scenes/audio_clip/audio_clip_scene.h:423`), is
	//     in the guest's registry but no production path pushes it onto the guest's stack. **A future
	//     `is_root()` override on a scene the guest hosts would break this key**: Shift+Select over
	//     that scene would re-root a note-mode session at `Settings`, and this function would skip the
	//     note exit on close.
	//   * A song swap resets the root to `Settings` (`scenes/menu/menu_scene.cpp:576-579`).
	switch (modeForRoot(deluge::scenes::menu_scene().menu_context().root)) {
	case NoteModeKind::NoteEditor:
		noteModeLegacyCalls().exitNoteEditor(); // `gui/ui/sound_editor.cpp:750`
		// `:751-752`, "refresh grid to potentially unhighlight edited notes": `uiNeedsRendering(this,
		// 0xFFFFFFFF, 0)`, where `this` is the menu UI about to close -- under the toggle, this guest.
		// It queues every main row against the guest's place in the hierarchy (`gui/ui/ui.cpp:279-303`),
		// and legacy's `close()` follows with the rows still pending (`sound_editor.cpp:761`), as
		// `closeGuest`'s does here.
		uiNeedsRendering(&shellHostUI, 0xFFFFFFFF, 0);
		break;
	case NoteModeKind::NoteRowEditor:
		noteModeLegacyCalls().exitNoteRowEditor(); // `gui/ui/sound_editor.cpp:754-755`, no repaint
		break;
	case NoteModeKind::None:
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// `util/note_editor_open.h` (note-family Task 7a)

bool noteEditorOpen() {
	// Legacy's expression, then its port for the guest: `inNoteEditor()` reads
	// `menuItemNavigationRecord[0]` (`gui/ui/sound_editor.cpp:1916-1918`), which no guest path writes,
	// and `activeNoteMode()` is that read's port. Off the guest's stack the second half is false.
	return (getCurrentUI() == &soundEditor && soundEditor.inNoteEditor())
	       || activeNoteMode() == NoteModeKind::NoteEditor;
}

bool noteRowEditorOpen() {
	// `noteEditorOpen`'s shape, over `inNoteRowEditor()` (`gui/ui/sound_editor.cpp:1920-1922`).
	return (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor())
	       || activeNoteMode() == NoteModeKind::NoteRowEditor;
}

// ---------------------------------------------------------------------------------------------
// The note-mode handlers' own menu calls (note-family Task 7a)

namespace {

/// @brief `MenuItem::shouldEnterSubmenu()` for the eight items `toggleNoteEditorParamMenu` can target.
///
/// The six note and row leaves are `note::SelectedNote`s and `note_row::SelectedNoteRow`s, whose
/// `selectEncoderActionIsPermitted()` is `return shouldEnterSubmenu();` (`note/selected_note.h:42`,
/// `note_row/selected_note_row.h:43`) -- and `Binding::permitted` is that function's port
/// (`scenes/menu/nodes/note.h`'s note on the two guards), popup included. The two roots are
/// `HorizontalMenu`s, which take `MenuItem`'s `return true` (`gui/menu_item/menu_item.h:271`); their
/// nodes carry no `permitted` hook, and that answers the same.
bool shouldEnterSubmenu(const Node& item, const MenuContext& ctx) {
	return item.bind.permitted == nullptr || item.bind.permitted(ctx);
}

} // namespace

bool noteModeCurrentMenuItemIs(NoteModeMenuItem item) {
	// `getCurrentMenuItem()` is `menuItemNavigationRecord[navigationDepth]`
	// (`gui/ui/sound_editor.cpp:1908-1910`); `MenuScene::focused()` is `path[depth]`, its port.
	const Node* const focused = deluge::scenes::menu_scene().focused();
	switch (item) {
	case NoteModeMenuItem::NoteEditorRoot:
		return focused == &nodes::note_editor_root;
	case NoteModeMenuItem::NoteRowEditorRoot:
		return focused == &nodes::note_row_editor_root;
	case NoteModeMenuItem::SequenceDirection:
		return focused == &nodes::sequence_direction;
	}
	return false;
}

void noteModeReadValueAgain() {
	// `MenuScene::read_value_again` is `MenuItem::readValueAgain`'s port, the one
	// `services::ModKnobs` already calls for `gui/views/view.cpp:1392`'s own `readValueAgain()`.
	deluge::scenes::menu_scene().read_value_again();
}

void noteModeGoUpOneLevel() {
	if (!deluge::scenes::menu_scene().go_up_one_level()) {
		// `gui/ui/sound_editor.cpp:715-717`'s `exitCompletely()`, ported as `shell_pad_forward.cpp`
		// ports `:1545`'s: remove the menu, and let the guest close itself (`closePortedMenu`'s own
		// docblock). The note arm of that exit runs from `closeGuest` (`exitNoteModeBeforeClose`).
		closePortedMenu();
	}
}

void noteModeToggleNoteEditorParamMenu(int32_t on) {
	deluge::scenes::menu::MenuScene& menu = deluge::scenes::menu_scene();
	const Node* const currentMenuItem = menu.focused();                         // `sound_editor.cpp:1926`
	const Node* newMenuItem = nullptr;                                          // `:1927`
	const bool inHorizontalMenu = runtimeFeatureSettings.isOn(HorizontalMenus); // `:1928`

	// if you're holding down a note
	// see if you're currently editing a row param
	// if so, enter the note editor menu for that param
	if (on) {
		if (currentMenuItem == &nodes::note_row_probability) { // `:1934-1936`
			newMenuItem = &nodes::note_editor_probability;
		}
		else if (currentMenuItem == &nodes::note_row_iterance) { // `:1937-1939`
			newMenuItem = &nodes::note_iterance;
		}
		else if (currentMenuItem == &nodes::note_row_fill) { // `:1940-1942`
			newMenuItem = &nodes::note_fill;
		}
		else if (currentMenuItem == &nodes::note_row_editor_root && inHorizontalMenu) { // `:1943-1945`
			newMenuItem = &nodes::note_editor_root;
		}
	}
	// if you're release a note
	// see if you're currently editing a note param
	// if so, enter the note row editor menu for that param
	else {
		if (currentMenuItem == &nodes::note_editor_probability) { // `:1951-1953`
			newMenuItem = &nodes::note_row_probability;
		}
		else if (currentMenuItem == &nodes::note_iterance) { // `:1954-1956`
			newMenuItem = &nodes::note_row_iterance;
		}
		else if (currentMenuItem == &nodes::note_fill) { // `:1957-1959`
			newMenuItem = &nodes::note_row_fill;
		}
		else if (currentMenuItem == &nodes::note_editor_root && inHorizontalMenu) { // `:1960-1962`
			newMenuItem = &nodes::note_row_editor_root;
		}
	}

	if (newMenuItem != nullptr && shouldEnterSubmenu(*newMenuItem, menu.menu_context())) { // `:1965`
		// holding down note pad, enter note param menu
		if (on) {
			// `:1968`'s `enterSubmenu`. **Not `MenuScene::open()`**, which would replace the context and
			// with it the root `activeNoteMode()` answers from: legacy's `inNoteRowEditor()` stays true
			// through the hold because `enterSubmenu` leaves `menuItemNavigationRecord[0]` alone.
			menu.enter_submenu(*newMenuItem);
		}
		// release note pad, go back to previous row menu
		else {
			noteModeGoUpOneLevel(); // `:1972`
		}
	}
}

const NoteModeLegacyCalls& noteModeLegacyCalls() {
	return *installed;
}

void setNoteModeLegacyCalls(const NoteModeLegacyCalls* calls) {
	installed = (calls != nullptr) ? calls : &kRealCalls;
}

} // namespace deluge::gui::ui
