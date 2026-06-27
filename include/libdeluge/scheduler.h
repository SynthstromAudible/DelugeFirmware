/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// libdeluge/scheduler.h — cooperative task scheduling.
///
/// The scheduler is a boundary service. Its canonical definition is the existing
/// `OSLikeStuff/scheduler_api.h` (already a clean C API: opaque ids, plain
/// functions). It is re-exported here so application code includes it via
/// `<libdeluge/...>` like every other service.
///
/// MIGRATION: in a later step `scheduler_api.h` (and `clock_type.h`) move
/// physically under `include/libdeluge/` and this forwarding include is dropped.
/// Until then this header forwards to the current location so there is a single
/// source of truth and no risk of drift.
///
/// ON EMBASSY: this same C API can be implemented on top of the Embassy executor
/// (Stage 2 in docs/dev/libdeluge_bsp_design.md) — `addRepeatingTask` -> spawn a
/// task with an embassy-time ticker, `RESOURCE_*` -> embassy-sync mutexes. The
/// application keeps this interface either way.
#ifndef LIBDELUGE_SCHEDULER_H
#define LIBDELUGE_SCHEDULER_H

// NOTE: relative include reflects the pre-migration source layout; it is the one
// include in this directory that intentionally reaches back into OSLikeStuff and
// will be replaced by a physical move.
#include "scheduler_api.h" // IWYU pragma: export

#endif // LIBDELUGE_SCHEDULER_H
