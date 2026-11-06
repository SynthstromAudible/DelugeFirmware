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

#include <cstdint>
#include <optional>

/// @file
/// @brief "Is the sound menu open?" -- legacy's own question, asked so that it also sees the ported menu.
///
/// The automation view and the automation display ask `getCurrentUI() == &soundEditor` (and, for the sound menu
/// proper, `!soundEditor.inSettingsMenu()`), which is false under the guest, where the current UI is `ShellHostUI`.
/// Each function below is legacy's expression or the guest's answer: the ported menu frontmost on the guest
/// (`shellSoundMenuOpen`). With the guest closed each is exactly legacy's expression. Header in `util/` and definitions
/// in `gui/ui/sound_menu_open.cpp`, on `note_editor_open.h`'s terms.

namespace deluge::gui::ui {

/// @brief Legacy's `getCurrentUI() == &soundEditor && !soundEditor.inSettingsMenu()`, or the ported menu frontmost on a
///        root other than the settings.
bool soundMenuOpen();

/// @brief Legacy's `getCurrentUI() == &soundEditor`, or the ported menu frontmost on a root other than the settings.
bool soundMenuCurrent();

/// @brief The open menu re-reads its value: legacy's `getCurrentMenuItem()->readValueAgain()` when `soundEditor` is
///        current, or the ported menu's.
void readSoundMenuValueAgain();

/// @brief Legacy's `getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu`,
///        or the ported menu frontmost on the note-range picker.
bool rangePickerOpen();

/// @brief Legacy's "selecting ranges" test -- the sample browser or audio recorder current, or the sound editor on a
///        range-dependent item -- or the guest's ported browser, recorder or range-dependent item.
bool rangeDependentMenuOpen();

/// @brief Legacy's `soundEditor.isUntransposedNoteWithinRange(note)`, or the ported menu's range when the guest is
///        current.
/// @param untransposed The note as the clip has it, before the sound's transpose.
bool noteWithinCurrentRange(int32_t untransposed);

/// @brief The keyboard DX column's row that shows white now: legacy's `dxParam.flash_row` while `soundEditor` is
///        current on the parameter page and `dxParam.blink_next` is set, or the ported page's
///        (`MenuScene::dx_column_flash`).
/// @return The row, or nothing when no row is white.
std::optional<int32_t> dxColumnFlashRow();

/// @brief Legacy's `menu_item::multiRangeMenu.noteOnToChangeRange(note)`, or the ported picker's.
/// @param note The note, transposed as the sound plays it.
void changeRangeForNote(int32_t note);

} // namespace deluge::gui::ui
