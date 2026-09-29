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

/// @file
/// @brief Report a storage channel's no-BSP fallback being called after boot.
///
/// Each channel's C ABI (`storage/*_abi.cpp`) has a weak fallback for builds with no driver behind it: the C-host sim
/// and the unit tests, neither of which ever finishes booting. A fallback that answers after boot therefore means a
/// build that boots but links no driver for that channel, and the fallback answers quietly: a collect that says
/// "unknown" forever stalls its caller until its tick bound runs out, and one that does the work inline takes the
/// synchronous arm the channel exists to avoid. On the host, the first call of each fallback after boot says so on
/// stderr and every call is counted, so a harness can fail the run (golden_vt_render exits 4). The device image links a
/// driver for every channel, so its fallbacks never run.

#ifdef DELUGE_HOST
namespace deluge::storage {
/// @brief Note that the fallback @p name was called: said once per name on stderr, and counted, if boot has finished.
void noteFallbackCalled(const char* name) noexcept;
} // namespace deluge::storage
#define DELUGE_FALLBACK_CALLED(name) ::deluge::storage::noteFallbackCalled(name)
#else
#define DELUGE_FALLBACK_CALLED(name) ((void)0)
#endif

extern "C" {
/// @brief Fallback calls counted after boot (host builds; see deluge::storage::noteFallbackCalled).
uint32_t deluge_storage_fallback_calls(void);
}
