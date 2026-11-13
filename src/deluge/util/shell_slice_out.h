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
/// @brief Whether the ported slicer has a slice out, for the layers that may not include `gui/`.
///
/// `util/notify.h`'s shape: the declaration here, the definition in `gui/ui/shell_slice_ops.cpp`. A song load may not
/// start while a slice is out (`PlaybackHandler`'s MIDI next-song holds it back): the slice lands its drums in the kit
/// it was asked for, and only the guest's tick finishes it.

namespace deluge::gui::ui {

/// @return Whether a slice is out. The guest refuses to close while one is: its drums land in the kit it was asked for.
bool shellSliceOut();

} // namespace deluge::gui::ui
