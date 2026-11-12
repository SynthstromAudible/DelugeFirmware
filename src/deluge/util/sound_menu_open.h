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

#include "definitions_cxx.hpp" // ActionResult

#include <cstdint>
#include <optional>

class ModControllableAudio;
class ParamManagerForTimeline;

/// @file
/// @brief "Is the sound menu open?", asked by the views, the keyboard screen and the automation display of the menu
///        that runs on the guest, where the current UI is `ShellHostUI`. Header in `util/` and definitions in
///        `gui/ui/sound_menu_open.cpp`, on `note_editor_open.h`'s terms.

namespace deluge::gui::ui {

/// @brief The ported menu frontmost on a root other than the settings (`shellSoundMenuOpen`).
bool soundMenuOpen();

/// @brief The kit-global-FX latch (`shellKitGlobalFx`), which the menu's shortcut presses read and write.
bool soundMenuKitGlobalFx();

/// @brief The owner the sound menu edits (`shellMenuModControllable`). Read only while `soundMenuOpen()`.
ModControllableAudio* soundMenuModControllable();

/// @brief The params the sound menu edits (`shellMenuParamManager`). Safe on the audio tier.
ParamManagerForTimeline* soundMenuParamManager();

/// @brief The ported menu frontmost on any root (`shellMenuFrontmost`).
bool menuCurrent();

/// @brief The menu re-reads its value when it is frontmost, whatever its root (both of `DISPLAY_AUTOMATION`'s arms).
void readSoundMenuValueAgain();

/// @brief An MPE zone may have changed (an MPE configuration message, a device file read): the member-channel page
///        re-reads when it is frontmost.
void mpeZonesPotentiallyUpdated();

/// @brief The ported browser, marker editor or drum rename is the guest's input top (`shellFreezesDrumSelection`):
///        the UIs under which `setSelectedDrum` keeps the selection.
bool drumSelectionFrozen();

/// @brief The menu frontmost on the note-range picker.
bool rangePickerOpen();

/// @brief The guest shows a range-dependent item, the range picker, the browser or the recorder.
bool rangeDependentMenuOpen();

/// @brief Whether @p untransposed falls in the menu's current range; true when no range-dependent item is open
///        (callers ask `rangeDependentMenuOpen()` first).
/// @param untransposed The note as the clip has it, before the sound's transpose.
bool noteWithinCurrentRange(int32_t untransposed);

/// @brief The keyboard DX column's row that shows white now, from the ported DX parameter page
///        (`MenuScene::dx_column_flash`).
/// @return The row, or nothing when no row is white.
std::optional<int32_t> dxColumnFlashRow();

/// @brief The note-range picker moves to the range holding @p note, when it is open.
/// @param note The note, transposed as the sound plays it.
void changeRangeForNote(int32_t note);

/// @brief A Shift + main-pad press that a clip view, the keyboard screen or the performance view offers as a menu
///        shortcut; the ported menu answers it whole (`shellShortcutPadAction`).
ActionResult menuShortcutPadAction(int32_t x, int32_t y, bool on);

} // namespace deluge::gui::ui
