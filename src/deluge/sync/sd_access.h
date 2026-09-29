/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

namespace deluge::sync {

/// @brief Whether the SD card / FatFS is currently being accessed (a transfer or
/// filesystem operation is in flight).
///
/// The app-level reentrancy checkers (MIDI / SysEx / playback) query this to defer their
/// own SD/FatFS work rather than re-enter a non-reentrant FatFS mid-operation. Backed by
/// `deluge_storage_fs_busy()`, which each BSP provides: false where filesystem work runs
/// inline on the caller (cooperative/C-host), the FS-mutex-held state on Embassy.
bool sd_busy();

} // namespace deluge::sync
