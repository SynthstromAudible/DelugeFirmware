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
#include <string_view>

/// @file
/// @brief The ported slicer's host: `SlicerScene`'s `SliceOps`, performed with the same `SliceCore` the legacy slicer
///        drives.
///
/// This header names no `scenes/` type; `shell_slice_ops.cpp` is where the scene is reached.

namespace deluge::gui::ui {

/// @brief Install the host's `SliceOps` on `SlicerScene`.
void wireShellSliceOps();

/// @brief Open the slicer on @p path, which the ported browser parked as a slice.
///
/// Loads the file into the browser preview's drum, without sounding it, as the legacy preview does; once it lands
/// the slicer holds it and the browser is told to push it (`BrowserScene::slice_ready`). A load that fails shows the
/// card error, and the browser and its menu stay as they were.
/// @param path The file, as the browser names it.
void openShellSlice(std::string_view path);

/// @brief Advance the slicer's loads: the open, a held audition, and a slice. Called on every guest tick.
void pollShellSlicer();

/// @return Whether one of the slicer's loads is out, so the guest keeps ticking to finish it.
bool shellSlicerBusy();

/// @brief How often the guest ticks while the slicer has a load out.
inline constexpr int32_t kShellSlicerPollMs = 15;

} // namespace deluge::gui::ui
