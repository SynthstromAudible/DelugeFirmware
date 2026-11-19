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

#pragma once

/// @file
/// @brief The **pad** half of the seam `shell_choice_dispatch.h` is the noun half of and
///        `shell_action_dispatch.h` is the verb half of -- what a grid pad the menu did not
///        claim is forwarded to.
///
/// The producing half is `ShellHostUI::padAction`: it calls `forwardGridPad` below for a pad
/// `MenuScene::grid_pad_claim()` did not claim. This file is the whole
/// of the consuming half, ported from `SoundEditor::padAction`'s root switch
/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) and its tail -- the part of that function
/// that runs only once `potentialShortcutPadAction` has already said no.
///
/// The firmware-version diagnostics inside the tail are `PadForwardTarget::FirmwareDiagnostic`: the
/// three pads pop up their number through the shell's notices and keep the menu open. The
/// `automationView` arm is **not** a gap: it is ported as
/// `PadForwardTarget::Automation` -- see `menuShowsAutomationParam` in the
/// `.cpp` for why that arm asks the menu rather than calling
/// `SoundEditor::handleAutomationViewPadAction` directly. The two note-mode arms inside the
/// `instrumentClipView` case are not a gap either: they are keyed on
/// `SoundEditor::inNoteEditor()` / `inNoteRowEditor()`, which read a `SoundEditor` member no guest
/// path writes, so they are ported as `PadForwardTarget::NoteEditorEdit`
/// and `PadForwardTarget::NoteRowEditor`, asked of `activeNoteMode()` (`gui/ui/shell_note_mode.h`)
/// instead. None of the arms below asks anything `GridPadClaim` answers -- that question is asked
/// by the caller before this function is ever reached.
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// The same two reasons `shell_choice_dispatch.h` and `shell_action_dispatch.h` give for the same
/// shape. **In `gui/ui/`** because deciding an arm is exactly the half that gets to name
/// `keyboardScreen`, `instrumentClipView` and `performanceView` -- the symbols `src/scenes` may not
/// include (`scenes-includes-outside`, `scripts/check_layer_rules.py`). **Its own translation unit**
/// for the same `deluge_scenes` closure reason `shell_action_dispatch.h` gives.

#include "definitions_cxx.hpp" // kDisplayWidth, kSideBarWidth
#include "gfx/grid/rgb.h"

#include <cstdint>

namespace deluge::gui::ui {

/// @brief Where a pad the menu did not claim was sent.
///
/// One value per arm of `SoundEditor::padAction`'s root switch (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)
/// plus its tail. Returned so a caller -- and a spec -- can see the decision the
/// switch made without inspecting what it did.
enum class PadForwardTarget : uint8_t {
	/// No arm matched and the tail's conditions did not hold. Main returns `DEALT_WITH` here too (the final `return` of
	/// `SoundEditor::padAction`): the pad is swallowed, which is what an open menu should do with it.
	None,
	/// The keyboard arm -- `keyboardScreen.padAction`, every coordinate.
	Keyboard,
	/// The audition-column arm -- `instrumentClipView.padAction`, the audition column only.
	AuditionColumn,
	/// The performance-view arm -- `performanceView.padAction`, every coordinate, never reaching the tail.
	Performance,
	/// The automation arm -- main's `SoundEditor::handleAutomationViewPadAction`'s work
	/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), done here: see `menuShowsAutomationParam` in the `.cpp` for
	/// why it is not a call into that function.
	Automation,
	/// The tail's `exitCompletely()`, ported as `closePortedMenu()`. Not returned for the three firmware-version
	/// diagnostic pads, which are `FirmwareDiagnostic`. Also the note editor's sidebar half (the note-editor arm's
	/// sidebar branch), which is the same `exitCompletely()` with no `on` or UI-mode test in front of it.
	Exit,
	/// The tail's firmware-version arm: on the firmware-version item, a press on (13, 7), (14, 7) or (15, 7) pops up
	/// the PIC firmware version, the voice count or the internal memory's size, and the menu stays open.
	FirmwareDiagnostic,
	/// The note-editor arm -- `instrumentClipView.handleNoteEditorEditPadAction`, the main grid only, while
	/// the note editor is on. Performed through `noteModeLegacyCalls()` (`gui/ui/shell_note_mode.h`).
	NoteEditorEdit,
	/// The note-row-editor arm -- `instrumentClipView.handleNoteRowEditorPadAction`, every coordinate, while the
	/// note-row editor is on; `closePortedMenu()` when it answers false.
	NoteRowEditor,
};

/// @brief What performs a decided forward. See `setPadForwardPerform`.
using PadForwardPerform = void (*)(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity);

/// @brief Route one grid pad the menu did not claim, exactly as `SoundEditor::padAction` does.
/// @param x,y The pad, in `MatrixDriver` coordinates.
/// @param velocity Main's `on`: non-zero is a press.
/// @return Which arm matched -- already performed when this returns.
PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity);

/// @brief Whether the root view keeps a shift-held main-grid pad that would otherwise be a menu shortcut.
///
/// `SoundEditor::potentialShortcutPadAction`'s carve-out: the automation view's interpolation and pad-selection pads,
/// x 0 at y 6 and 7, are no shortcuts while it is the root. A pad kept here goes to `forwardGridPad`.
/// @param x,y The pad, in `MatrixDriver` coordinates.
/// @return True when the pad is the root view's.
bool rootKeepsShortcutPad(int32_t x, int32_t y);

/// @brief Whether the root view takes the audition column under the sample browser: the instrument clip view, which
///        `SampleBrowser::padAction` hands it to.
bool rootTakesAuditionPad();

/// @brief What `SoundEditor::potentialShortcutPadAction` does with a main-grid press under the menu.
enum class ShortcutPadGate : uint8_t {
	Forward,  ///< not a shortcut: the press goes on to the root's arms (`forwardGridPad`)
	Swallow,  ///< a shortcut press with a UI mode outside the two shortcut modes up: taken, and nothing happens
	Shift,    ///< a shortcut, by SHIFT
	Audition, ///< a shortcut, by an audition held over the instrument clip view
};

/// @brief `potentialShortcutPadAction`'s gate for a press inside the main grid: SHIFT, or else an audition held over
///        the instrument clip view; then the UI modes it allows, `UI_MODE_AUDITIONING` and
///        `UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR`, outside which the press is swallowed.
/// @param shift Whether SHIFT is held (`Buttons::isShiftButtonPressed()`).
ShortcutPadGate shortcutPadGate(bool shift);

/// @brief `potentialShortcutPadAction`'s note-row editor shortcut under the menu: on the sequence-direction
///        pad, over an instrument clip, with HorizontalMenus on and exactly one row auditioning, the note-row editor
///        opens on PLAY DIRECTION (`InstrumentClipView::enterNoteRowEditor`, which re-roots the open menu).
/// @return Whether the press was this shortcut's (taken, whatever the editor then did).
bool takeSequenceDirectionShortcut(int32_t x, int32_t y);

/// @brief `SampleMarkerEditor::padAction`'s audition arm: the audition column to the instrument clip view, press and
///        release, when the clip is an instrument clip, whatever the root view; swallowed on an audio clip.
/// @param x,y The pad, in `MatrixDriver` coordinates.
/// @param velocity Main's `on`.
void forwardClipAuditionPad(int32_t x, int32_t y, int32_t velocity);

/// @brief `SoundEditor::isEditingAutomationViewParam`, asked of the menu: whether the param the menu edits is
///        the one the automation view has selected.
bool menuShowsAutomationViewParam();

/// @brief Replace what performs a decided forward; `nullptr` restores the real one.
///
/// `shell_choice_dispatch.h`'s `setSampleClaimDispatch` for the same reason it exists there: the
/// decision is the part worth pinning, and performing it means calling into `keyboardScreen` /
/// `instrumentClipView` / `performanceView`, whose handlers need model state a spec should not have
/// to build. `deluge_scenes_spec` links `deluge_app`, so those symbols resolve -- this is about what
/// running them costs, not about what links.
void setPadForwardPerform(PadForwardPerform perform);

/// @brief Draw the sidebar the sample marker editor shows over its own waveform.
///
/// `SampleMarkerEditor::renderSidebar` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`), whole: over
/// the keyboard screen it draws `instrumentClipView`'s sidebar, and over any other root it leaves
/// the sidebar to the UI underneath. The marker scene may not read the root UI, so the guest
/// asks here on its behalf. Same arguments and answer as `UI::renderSidebar`, query form included.
/// @param whichRows Bit `y` selects row `y`.
/// @param image Legacy's pad image, or null to ask without drawing.
/// @param occupancyMask Legacy's occupancy map, passed through.
/// @return True when the clip view's sidebar was drawn (or would be), false to obey the UI underneath.
bool renderClipViewSidebarUnderMarker(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

} // namespace deluge::gui::ui
