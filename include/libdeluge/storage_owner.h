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

#ifndef LIBDELUGE_STORAGE_OWNER_H
#define LIBDELUGE_STORAGE_OWNER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief True when a filesystem operation is in flight — i.e. touching the filesystem now would
///        block until it completes.
///
/// Cooperative/C-host BSPs: always false (filesystem work runs inline on the caller, so no other context
/// can hold the filesystem). Embassy: true while the efatfs FS mutex is held by a storage task.
/// @note Safe to call from ANY context and never blocks — off-owner callers querying "may I start
///       filesystem work now?" are the primary consumer.
/// @return true iff a filesystem operation is currently in flight.
bool deluge_storage_fs_busy(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_STORAGE_OWNER_H
