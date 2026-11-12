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
/// @brief "Does the sample preview sound?" The audio engine renders the preview drum only while the sample browser,
///        one of its two long-press menus or the slicer is frontmost on the guest at its last tick
///        (`shellSamplePreviewSounds`, which the audio tier reads without walking the guest's stack). Header in
///        `util/` and definition in `gui/ui/sample_preview_open.cpp`, on `sound_menu_open.h`'s terms.

namespace deluge::gui::ui {

/// @brief Whether the ported sample browser, one of its folder menus or the slicer is frontmost on the guest
///        (`shellSamplePreviewSounds`).
bool samplePreviewUIOpen();

} // namespace deluge::gui::ui
