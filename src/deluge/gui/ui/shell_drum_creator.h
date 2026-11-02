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

class InstrumentClip;
class SoundDrum;

/// @file
/// @brief The drum creator through the guest: `InstrumentClipView::enterDrumCreator`'s sample browser, ported, and
///        the drum `SampleBrowser::exitAction` deletes when it is backed out of with no file.

namespace deluge::gui::ui {

/// @brief Open the ported sample browser on @p drum, the drum the creator has just made and selected.
///
/// The guest lands on the drum's file selector, as a shortcut to it lands, so the browser is what shows and its BACK
/// closes the guest. @p drum is remembered for `discardShellDrumCreatorBeforeClose`.
/// @param clip The kit clip the drum was made in.
/// @param drum The new drum.
/// @return Whether the guest is now open; false leaves nothing open and nothing remembered.
bool openShellDrumCreator(InstrumentClip* clip, SoundDrum* drum);

/// @brief `SampleBrowser::exitAction`'s drum-creator arm, run as the guest closes.
///
/// When the guest was opened by `openShellDrumCreator`, the drum it made is deleted if its file was never chosen and
/// the kit holds a drum no row of the current clip uses -- legacy's condition, unchanged. A drum that is no longer in
/// the current kit (an import or a slice replaced it) is left alone. Forgets the drum either way.
void discardShellDrumCreatorBeforeClose();

} // namespace deluge::gui::ui
