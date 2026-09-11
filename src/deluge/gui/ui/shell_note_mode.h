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
/// @brief The note editor's and note-row editor's routing through the guest: what `SoundEditor`
///        does with a button, an encoder turn and its own exit while `inNoteEditor()` or
///        `inNoteRowEditor()` holds, ported onto `ShellHostUI`.
///
/// `SoundEditor` asks "am I in a note mode?" at five sites of its own (design spec §4's table): the
/// button chain (`gui/ui/sound_editor.cpp:617-623`), the exit (`:749-755`), the horizontal encoder
/// (`:1047-1052`), the pad switch (`:1481-1498`) and the vertical encoder (`:1593-1598`). The pad
/// switch's port is `gui/ui/shell_pad_forward.cpp`'s; the other four are the functions below, one
/// per `ShellHostUI` hook. Each answers `std::nullopt` for "not the note mode's" -- the hook then
/// feeds the shell exactly as it did before this file existed -- or the legacy handler's own
/// `ActionResult`, which the hook returns.
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// `shell_pad_forward.h`'s two reasons. **In `gui/ui/`** because the half that forwards gets to name
/// `instrumentClipView`, which `src/scenes` may not. **Its own translation unit** because
/// `activeNoteMode` reads `deluge::scenes::menu_scene()` -- the `deluge_scenes` closure the host
/// spec suite never builds (`tests/spec/CMakeLists.txt`) -- so `shell_host_ui.cpp` names only the
/// four functions, and `tests/spec/gui/support/fake_legacy_ui.cpp` fakes them. This header names no
/// `scenes/` or `shell/` type, so it is not a `LEGACY_PORT_SEAMS` entry; its `.cpp` is.

#include "definitions_cxx.hpp" // ActionResult
#include "hid/button.h"

#include <cstdint>
#include <optional>

namespace deluge::gui::ui {

/// @brief Which of `SoundEditor`'s two note modes the guest is hosting, if either.
enum class NoteModeKind : uint8_t {
	/// Neither: every hook below answers "not mine".
	None,
	/// `SoundEditor::inNoteEditor()` (`gui/ui/sound_editor.cpp:1916-1918`).
	NoteEditor,
	/// `SoundEditor::inNoteRowEditor()` (`:1920-1922`).
	NoteRowEditor,
};

/// @brief The port of `inNoteEditor()` / `inNoteRowEditor()` for input: the ported menu's root, while
///        the ported menu is on the guest's scene stack.
///
/// On the stack, not frontmost: legacy reads its navigation record's first entry, not the current
/// UI, so a popup or an input-taking scene over the menu leaves the mode on. Off the stack it is
/// `None`, whatever `menu_context().root` still says.
NoteModeKind activeNoteMode();

/// @brief Site 2: `SoundEditor::buttonAction`'s two note arms (`gui/ui/sound_editor.cpp:617-623`).
/// @return `std::nullopt` for a button that is not the note mode's, else the handler's own result.
std::optional<ActionResult> forwardNoteButton(deluge::hid::Button b, bool on, bool inCardRoutine);

/// @brief Site 6: `SoundEditor::horizontalEncoderAction`'s two note arms (`:1047-1052`).
/// @return `std::nullopt` outside the note modes, else the handler's own result.
std::optional<ActionResult> forwardNoteHorizontalEncoder(int32_t offset);

/// @brief Site 7: `SoundEditor::verticalEncoderAction`'s two note arms (`:1593-1598`).
/// @return `std::nullopt` outside the note modes, else the handler's own result.
std::optional<ActionResult> forwardNoteVerticalEncoder(int32_t offset, bool inCardRoutine);

/// @brief Site 4: `SoundEditor::exitCompletely`'s two note arms (`:749-755`), for `closeGuest`.
///
/// Keyed on `menu_context().root` alone, **not** on `activeNoteMode()`, and that is forced: both of
/// the guest's close paths reach `closeGuest` after the menu has already left the stack --
/// `exitUI` pops first (`gui/ui/shell_host_ui.cpp`), and `timerCallback` closes on an empty stack.
/// See the `.cpp` for why the root is still this session's there.
void exitNoteModeBeforeClose();

/// @brief Every legacy call the note mode makes into `instrumentClipView`, as replaceable pointers.
///
/// `shell_pad_forward.h`'s `setPadForwardPerform`, for the same reason and one more: several of
/// these handlers dereference `soundEditor.getCurrentMenuItem()`, which is null under the guest
/// (`gui/views/instrument_clip_view.cpp:3655`, `:3690-3691`, `:3836`, `:3842`, `:3878`,
/// `:3905-3907`), so a spec cannot drive them with the ported menu open until legacy's side sees
/// the ported mode. The decisions around them are what a spec pins through these.
struct NoteModeLegacyCalls {
	/// `InstrumentClipView::handleNoteEditorButtonAction`.
	ActionResult (*noteEditorButton)(deluge::hid::Button b, bool on, bool inCardRoutine);
	/// `InstrumentClipView::handleNoteRowEditorButtonAction`.
	ActionResult (*noteRowEditorButton)(deluge::hid::Button b, bool on, bool inCardRoutine);
	/// `InstrumentClipView::handleNoteEditorHorizontalEncoderAction`.
	ActionResult (*noteEditorHorizontal)(int32_t offset);
	/// `InstrumentClipView::handleNoteRowEditorHorizontalEncoderAction`.
	ActionResult (*noteRowEditorHorizontal)(int32_t offset);
	/// `InstrumentClipView::handleNoteEditorVerticalEncoderAction`.
	ActionResult (*noteEditorVertical)(int32_t offset, bool inCardRoutine);
	/// `InstrumentClipView::handleNoteRowEditorVerticalEncoderAction`.
	ActionResult (*noteRowEditorVertical)(int32_t offset, bool inCardRoutine);
	/// `InstrumentClipView::handleNoteEditorEditPadAction`, for `shell_pad_forward.cpp`.
	void (*noteEditorEditPad)(int32_t x, int32_t y, int32_t on);
	/// `InstrumentClipView::handleNoteRowEditorPadAction`, for `shell_pad_forward.cpp`.
	bool (*noteRowEditorPad)(int32_t x, int32_t y, int32_t on);
	/// `InstrumentClipView::exitNoteEditor`.
	void (*exitNoteEditor)();
	/// `InstrumentClipView::exitNoteRowEditor`.
	void (*exitNoteRowEditor)();
};

/// @brief The calls in force: the real ones unless `setNoteModeLegacyCalls` replaced them.
const NoteModeLegacyCalls& noteModeLegacyCalls();

/// @brief Replace every legacy call the note mode makes; `nullptr` restores the real ones.
/// @param calls Borrowed, not copied: it must outlive its installation.
void setNoteModeLegacyCalls(const NoteModeLegacyCalls* calls);

} // namespace deluge::gui::ui
