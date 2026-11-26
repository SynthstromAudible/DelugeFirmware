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
#include "model/lane_owner.h"

#include <cstdint>
#include <optional>

class ParamManagerForTimeline;

/// @file
/// @brief "Is the sound menu open?", asked by the views, the keyboard screen and the automation display of the menu
///        that runs on the guest, where the current UI is `ShellHostUI`. Header in `util/` on `note_editor_open.h`'s
///        terms; the answers are the guest's own: `gui/ui/shell_guest_services.cpp`, except the published owner and
///        params (`gui/ui/browser_host.cpp`) and the shortcut press (`gui/ui/menu_entry.cpp`).

namespace deluge::gui::ui {

/// @brief Whether the menu is the guest's frontmost scene, the guest is the current UI, and the menu's root is
///        not the settings.
bool soundMenuOpen();

/// @brief The owner the sound menu edits, as the menu last published it (`publishMenuTarget`). Read only while
///        `soundMenuOpen()`. Safe on the audio tier.
deluge::model::LaneOwner sound_menu_owner();

/// @brief The params the sound menu edits, as the menu last published them. Safe on the audio tier.
ParamManagerForTimeline* soundMenuParamManager();

/// @brief Whether the menu is the guest's frontmost scene and the guest is the current UI, whatever the menu's
///        root: the guest's `getCurrentUI() == &soundEditor`.
bool menuCurrent();

/// @brief The menu re-reads its focused value from the model (`MenuScene::read_value_again`) when it is frontmost,
///        whatever its root (both of `DISPLAY_AUTOMATION`'s arms).
void readSoundMenuValueAgain();

/// @brief An MPE zone may have changed (an MPE configuration message, a device file read): the menu frontmost on the
///        member-channel page re-reads it, as `SoundEditor::mpeZonesPotentiallyUpdated` re-reads
///        `zoneNumMemberChannelsMenu`.
void mpeZonesPotentiallyUpdated();

/// @brief Whether the guest is current with the sample browser, the sample marker editor or the drum rename as its
///        input top (`drumSelectionFrozenBy`): the UIs under which `InstrumentClipView::setSelectedDrum` keeps the
///        kit's selection.
bool drumSelectionFrozen();

/// @brief Whether the menu is frontmost on the note-range picker.
bool rangePickerOpen();

/// @brief Whether the guest's frontmost scene is the sample browser or the audio recorder, or the menu on a
///        range-dependent item or the picker.
bool rangeDependentMenuOpen();

/// @brief Whether @p untransposed falls in the range the menu has selected, on a source of several; true when
///        no range-dependent item is open (callers ask `rangeDependentMenuOpen()` first). Asks `Source::getRange`,
///        which remembers the range as legacy's test does.
/// @param untransposed The note as the clip has it, before the sound's transpose.
bool noteWithinCurrentRange(int32_t untransposed);

/// @brief The keyboard DX column's row that shows white now, from the ported DX parameter page
///        (`MenuScene::dx_column_flash` on the guest's clock), while the sound menu is open.
/// @return The row, or nothing when no row is white.
std::optional<int32_t> dxColumnFlashRow();

/// @brief The note-range picker moves to the range holding @p note, when it is open
///        (`MenuScene::select_range_for_note`).
/// @param note The note, transposed as the sound plays it.
void changeRangeForNote(int32_t note);

/// @brief `SoundEditor::potentialShortcutPadAction` for the menu, whole: what the clip views, the keyboard
///        screen and the performance view ask of a main-grid press.
///
/// The gate first (`shortcutPadGate`: SHIFT, or an audition held over the instrument clip view, inside the two
/// shortcut modes; the automation view's interpolation pads stay the view's). Then the item: a kit with nothing to edit
/// has none; a coming-soon cell says "Feature not (yet?) implemented"; the sequence-direction pad with one row
/// auditioning opens the note-row editor; any other pad goes to `openMenuAtShortcut`, which opens, re-roots or
/// says its refusal. Off a clip minder the context names no clip, so the song view's table and the song are read, as
/// legacy's `setup()` reads them.
/// @param x,y The pad.
/// @param on  Whether it is a press.
/// @return `NOT_DEALT_WITH` for a release, a sidebar pad or a press the gate passes on to the view; `DEALT_WITH`
///         otherwise, whether or not anything opened.
ActionResult menuShortcutPadAction(int32_t x, int32_t y, bool on);

} // namespace deluge::gui::ui
