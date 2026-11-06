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
/// @brief "Does the sample preview sound?" -- legacy's own question, asked so that it also sees the ported browser.
///
/// The audio engine renders the preview drum only while the sample browser, one of its two long-press menus or the
/// slicer is the current UI, which is false under the guest, where the current UI is `ShellHostUI`. This is legacy's
/// expression or the guest's answer: those UIs' ports frontmost on the guest at its last tick
/// (`shellSamplePreviewSounds`, which the audio tier reads without walking the guest's stack). With the guest closed it
/// is exactly legacy's expression. Header in `util/` and definition in `gui/ui/sample_preview_open.cpp`, on
/// `sound_menu_open.h`'s terms.

namespace deluge::gui::ui {

/// @brief Legacy's `getCurrentUI()` is the sample browser, its kit or synth long-press menu, or the slicer; or their
///        ports are frontmost on the guest.
bool samplePreviewUIOpen();

} // namespace deluge::gui::ui
