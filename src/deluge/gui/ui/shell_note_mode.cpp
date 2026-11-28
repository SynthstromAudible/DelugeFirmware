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
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h" // uiNeedsRendering
#include "gui/views/instrument_clip_view.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/note_editor_open.h"
#include "util/sound_menu_open.h" // menuCurrent

#include "ui/scenes/menu/chains.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/note.h"
#include "ui/scenes/registry.h"
#include "ui/shell/scene.h"

namespace deluge::gui::ui {
namespace {

using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
namespace nodes = deluge::ui::scenes::menu::nodes;

// The real calls, one per `instrumentClipView` member `SoundEditor` names for the mode.

ActionResult realNoteEditorButton(deluge::hid::Button b, bool on) {
	return instrumentClipView.handleNoteEditorButtonAction(b, on);
}

ActionResult realNoteRowEditorButton(deluge::hid::Button b, bool on) {
	return instrumentClipView.handleNoteRowEditorButtonAction(b, on);
}

ActionResult realNoteEditorHorizontal(int32_t offset) {
	return instrumentClipView.handleNoteEditorHorizontalEncoderAction(offset);
}

ActionResult realNoteRowEditorHorizontal(int32_t offset) {
	return instrumentClipView.handleNoteRowEditorHorizontalEncoderAction(offset);
}

ActionResult realNoteEditorVertical(int32_t offset) {
	return instrumentClipView.handleNoteEditorVerticalEncoderAction(offset);
}

ActionResult realNoteRowEditorVertical(int32_t offset) {
	return instrumentClipView.handleNoteRowEditorVerticalEncoderAction(offset);
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
///        `menuItemNavigationRecord[0]` against the two root items (`SoundEditor::inNoteEditor` in
///        `origin/main:src/deluge/gui/ui/sound_editor.cpp`), in the port's representation of "which root is open".
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
	// **On the guest's stack, not frontmost.** `menuCurrent` (`gui/ui/shell_guest_services.cpp`)
	// asks `input_top()`, which is the port of `getCurrentUI() == &soundEditor`. The note mode is not
	// that question: `inNoteEditor()` reads the navigation record, not the current UI, so it stays true
	// while a popup or another UI sits over the sound editor. So this walks every stacked scene, and a
	// scene over the menu -- input-taking or not -- leaves the answer alone.
	//
	// Identity against `menu_scene()`, for `menuCurrent`'s own reason: the registry maps several
	// application kinds, and only one object is the menu.
	const deluge::ui::shell::Scene* const menu = &deluge::ui::scenes::menu_scene();
	for (const deluge::ui::shell::Scene* scene : deluge::gui::ui::shellRuntime().shell().stack().scenes()) {
		if (scene == menu) {
			// `MenuScene::menu_context()` is replaced only by `MenuScene::open`
			// and by a song swap (`MenuScene::on_model_changed`, which resets the
			// root to `Settings`), so while the menu is stacked this is the root it was opened at.
			return modeForRoot(deluge::ui::scenes::menu_scene().menu_context().root);
		}
	}
	return NoteModeKind::None;
}

bool noteEditorOpen() {
	return activeNoteMode() == NoteModeKind::NoteEditor;
}

bool noteRowEditorOpen() {
	return activeNoteMode() == NoteModeKind::NoteRowEditor;
}

std::optional<ActionResult> forwardNoteButton(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;

	const NoteModeKind mode = activeNoteMode();
	if (mode == NoteModeKind::None) {
		// Neither of `SoundEditor::buttonAction`'s note arms' tests holds.
		return std::nullopt;
	}

	// **The six buttons main's chain takes before it reaches the note arms** (`SoundEditor::buttonAction`,
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`: its select-encoder, back, save, learn, keyboard and
	// session-view arms). Each is an outer `else if` on the button alone, so it consumes the press whether or not
	// its inner test holds, and none of them is the note handler's. Under the guest each is the menu's, fed.
	if (b == SELECT_ENC || b == BACK || b == SAVE || b == LEARN || b == KEYBOARD || b == SESSION_VIEW) {
		return std::nullopt;
	}

	// **SYNTH, KIT, MIDI and CV: the column focus, which is the menu's.** Main's two
	// handlers send these to `soundEditor.getCurrentMenuItem()->buttonAction(...)`
	// (`InstrumentClipView::handleNoteEditorButtonAction`, `handleNoteRowEditorButtonAction`,
	// `origin/main:src/deluge/gui/views/instrument_clip_view.cpp`, `util::one_of(b, {SYNTH, KIT, MIDI, CV})`), whose
	// target is the horizontal menu's column focus, which `MenuScene` carries. The handlers here have no such arm
	// (`gui/views/instrument_clip_view.cpp`), so forwarding these buttons would swallow them as `DEALT_WITH`; the
	// menu takes them instead.
	if (b == SYNTH || b == KIT || b == MIDI || b == CV) {
		return std::nullopt;
	}

	// **PLAY, SHIFT and the two gold-knob pushes: `NOT_DEALT_WITH`** in both handlers (the `NOT_DEALT_WITH` returns of
	// `InstrumentClipView::handleNoteEditorButtonAction` and `handleNoteRowEditorButtonAction`,
	// `gui/views/instrument_clip_view.cpp`), which is exactly what the guest's fed path already
	// returns -- so feeding them keeps that answer and also keeps the shell's own shift latch fed. (The gold-knob
	// pushes never get here: `ShellHostUI::buttonAction` returns for them first.)
	if (b == PLAY || b == SHIFT || b == MOD_ENCODER_0 || b == MOD_ENCODER_1) {
		return std::nullopt;
	}

	// **Everything else is the handler's, and its answer is the guest's answer.** AFFECT_ENTIRE gets here at all
	// because `SoundEditor`'s own AFFECT_ENTIRE arm is guarded on `!inNoteEditor() && !inNoteRowEditor()` (main's
	// `SoundEditor::buttonAction`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`). In the handlers, X_ENC and
	// SYNC_SCALING reach `InstrumentClipView::buttonAction` through their `return buttonAction(...)` arms
	// (`InstrumentClipView::handleNoteEditorButtonAction` and `InstrumentClipView::handleNoteRowEditorButtonAction` in
	// `gui/views/instrument_clip_view.cpp`); AFFECT_ENTIRE toggles a kit clip's `affectEntire` in the note-row editor
	// and is swallowed in the note editor; every other button, CROSS_SCREEN and SCALE included, is swallowed as
	// `DEALT_WITH` (the final `return` of each handler).
	const NoteModeLegacyCalls& calls = noteModeLegacyCalls();
	if (mode == NoteModeKind::NoteEditor) {
		return calls.noteEditorButton(b, on); // `SoundEditor::buttonAction`
	}
	return calls.noteRowEditorButton(b, on); // `SoundEditor::buttonAction`
}

std::optional<ActionResult> forwardNoteHorizontalEncoder(int32_t offset) {
	switch (activeNoteMode()) {
	case NoteModeKind::NoteEditor:
		// `SoundEditor::horizontalEncoderAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)
		return noteModeLegacyCalls().noteEditorHorizontal(offset);
	case NoteModeKind::NoteRowEditor:
		// `SoundEditor::horizontalEncoderAction`'s note-row-editor arm
		return noteModeLegacyCalls().noteRowEditorHorizontal(offset);
	case NoteModeKind::None:
		break;
	}
	return std::nullopt;
}

std::optional<ActionResult> forwardNoteVerticalEncoder(int32_t offset) {
	switch (activeNoteMode()) {
	case NoteModeKind::NoteEditor:
		// `SoundEditor::verticalEncoderAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): its `inNoteEditor()`
		// arm calls `handleNoteEditorVerticalEncoderAction`.
		return noteModeLegacyCalls().noteEditorVertical(offset);
	case NoteModeKind::NoteRowEditor:
		// `SoundEditor::verticalEncoderAction`'s `inNoteRowEditor()` arm.
		return noteModeLegacyCalls().noteRowEditorVertical(offset);
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
	//   * `openMenu`, the guest's one open site, runs `menu_scene().open(...)` before
	//     `openUI(&shellHostUI)` (`openGuest` in `gui/ui/menu_entry.cpp`), so every session begins by
	//     writing its own root.
	//   * `GlobalControls::handle_settings_menu`, the port
	//     of Shift+Select, re-opens the menu at `Root::Settings`, and the guest's shell does
	//     install `GlobalControls` (`gui/ui/shell_guest_services.cpp`). It cannot run during a
	//     guest session today: its arm is reached only when the input-taking scene is a root scene,
	//     `stack_.input_top().is_root()` (`GlobalControls::handle`). `Scene::is_root` defaults to false,
	//     and neither `MenuScene` nor `LoadingScene` overrides it. The one
	//     override that answers true, `AudioClipScene`, is
	//     in the guest's registry but no production path pushes it onto the guest's stack. **A future
	//     `is_root()` override on a scene the guest hosts would break this key**: Shift+Select over
	//     that scene would re-root a note-mode session at `Settings`, and this function would skip the
	//     note exit on close.
	//   * A song swap resets the root to `Settings` (`MenuScene::on_model_changed`).
	switch (modeForRoot(deluge::ui::scenes::menu_scene().menu_context().root)) {
	case NoteModeKind::NoteEditor:
		// `SoundEditor::exitCompletely`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`
		noteModeLegacyCalls().exitNoteEditor();
		// `exitCompletely`'s "refresh grid to potentially unhighlight edited notes" step is `uiNeedsRendering(this,
		// 0xFFFFFFFF, 0)`, where `this` is the menu UI about to close -- this guest.
		// `uiNeedsRendering` queues every main row against the guest's place in the hierarchy,
		// and legacy's `close()` follows with the rows still pending (`SoundEditor::exitCompletely`), as
		// `closeGuest`'s does here.
		uiNeedsRendering(&shellHostUI, 0xFFFFFFFF, 0);
		break;
	case NoteModeKind::NoteRowEditor:
		// `SoundEditor::exitCompletely`, with no repaint.
		noteModeLegacyCalls().exitNoteRowEditor();
		break;
	case NoteModeKind::None:
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// The note-mode handlers' own menu calls

namespace {

/// @brief `MenuItem::shouldEnterSubmenu()` for the eight items `toggleNoteEditorParamMenu` can target.
///
/// The six note and row leaves are `note::SelectedNote`s and `note_row::SelectedNoteRow`s, whose
/// `selectEncoderActionIsPermitted()` is `return shouldEnterSubmenu();` (`note::SelectedNote`,
/// `note_row::SelectedNoteRow`) -- and `Binding::permitted` is that function's port
/// (`ui/scenes/menu/nodes/note.h`'s note on the two guards), popup included. The two roots are
/// `HorizontalMenu`s, which take `MenuItem::shouldEnterSubmenu`'s `return true`; their
/// nodes carry no `permitted` hook, and that answers the same.
bool shouldEnterSubmenu(const Node& item, const MenuContext& ctx) {
	return item.bind.permitted == nullptr || item.bind.permitted(ctx);
}

} // namespace

bool noteModeCurrentMenuItemIs(NoteModeMenuItem item) {
	// `getCurrentMenuItem()` is `menuItemNavigationRecord[navigationDepth]` (`SoundEditor::getCurrentMenuItem` in
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`); `MenuScene::focused()` is `path[depth]`, its port.
	const Node* const focused = deluge::ui::scenes::menu_scene().focused();
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
	// `services::ModKnobs` already calls for `View`'s own `readValueAgain()`.
	deluge::ui::scenes::menu_scene().read_value_again();
}

bool noteModeVelocityShown() {
	// Main's rule: the note's velocity item, or the note editor's root drawn horizontally.
	if (!menuCurrent()) {
		return false;
	}
	const deluge::ui::scenes::menu::MenuScene& menu = deluge::ui::scenes::menu_scene();
	const Node* const focused = menu.focused();
	return focused == &nodes::note_velocity
	       || (focused == &nodes::note_editor_root
	           && deluge::ui::scenes::menu::renders_horizontal(nodes::note_editor_root, menu.menu_context()));
}

bool portedMenuOnAuditionedRowDirection() {
	// `getCurrentUI() == &soundEditor`: the guest current, with the menu taking its input.
	deluge::ui::scenes::menu::MenuScene& menu = deluge::ui::scenes::menu_scene();
	if (getCurrentUI() != &shellHostUI
	    || &deluge::gui::ui::shellRuntime().shell().stack().input_top()
	           != static_cast<deluge::ui::shell::Scene*>(&menu)) {
		return false;
	}
	return menu.menu_context().selected_note_row && menu.focused() == &nodes::sequence_direction;
}

void noteModeGoUpOneLevel() {
	if (!deluge::ui::scenes::menu_scene().go_up_one_level()) {
		// `SoundEditor::buttonAction`'s BACK arm's `exitCompletely()`, ported as `shell_pad_forward.cpp` ports
		// `SoundEditor::padAction`'s (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): remove the menu, and let the
		// guest close itself (`closePortedMenu`'s own docblock). The note arm of that exit runs from `closeGuest`
		// (`exitNoteModeBeforeClose`).
		closePortedMenu();
	}
}

void noteModeToggleNoteEditorParamMenu(int32_t on) {
	deluge::ui::scenes::menu::MenuScene& menu = deluge::ui::scenes::menu_scene();
	// Main's `SoundEditor::toggleNoteEditorParamMenu` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), arm for arm.
	const Node* const currentMenuItem = menu.focused();
	const Node* newMenuItem = nullptr;
	const bool inHorizontalMenu = runtimeFeatureSettings.isOn(HorizontalMenus);

	// if you're holding down a note
	// see if you're currently editing a row param
	// if so, enter the note editor menu for that param
	if (on) {
		if (currentMenuItem == &nodes::note_row_probability) {
			newMenuItem = &nodes::note_editor_probability;
		}
		else if (currentMenuItem == &nodes::note_row_iterance) {
			newMenuItem = &nodes::note_iterance;
		}
		else if (currentMenuItem == &nodes::note_row_fill) {
			newMenuItem = &nodes::note_fill;
		}
		else if (currentMenuItem == &nodes::note_row_editor_root && inHorizontalMenu) {
			newMenuItem = &nodes::note_editor_root;
		}
	}
	// if you're release a note
	// see if you're currently editing a note param
	// if so, enter the note row editor menu for that param
	else {
		if (currentMenuItem == &nodes::note_editor_probability) {
			newMenuItem = &nodes::note_row_probability;
		}
		else if (currentMenuItem == &nodes::note_iterance) {
			newMenuItem = &nodes::note_row_iterance;
		}
		else if (currentMenuItem == &nodes::note_fill) {
			newMenuItem = &nodes::note_row_fill;
		}
		else if (currentMenuItem == &nodes::note_editor_root && inHorizontalMenu) {
			newMenuItem = &nodes::note_row_editor_root;
		}
	}

	if (newMenuItem != nullptr && shouldEnterSubmenu(*newMenuItem, menu.menu_context())) {
		// holding down note pad, enter note param menu
		if (on) {
			// Main's `enterSubmenu`. **Not `MenuScene::open()`**, which would replace the context and
			// with it the root `activeNoteMode()` answers from: legacy's `inNoteRowEditor()` stays true
			// through the hold because `enterSubmenu` leaves `menuItemNavigationRecord[0]` alone.
			menu.enter_submenu(*newMenuItem);
		}
		// release note pad, go back to previous row menu
		else {
			noteModeGoUpOneLevel();
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
