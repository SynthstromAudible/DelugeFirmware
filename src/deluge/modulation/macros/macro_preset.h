/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "modulation/macros/macros.h"
#include "storage/storage_manager.h" // FilePointer (anonymous-struct typedef, can't be forward-declared)

// The load half of macro presets. Split out of the core macros.h so that header - which output.h
// includes - never pulls in storage_manager.h (which drags in generated headers that break TUs whose
// include paths can't reach the build/ dir). Loads a macro's targets from the pointed-to preset file:
// replaces macros[macroIndex]'s targets (keeping its source and active state), clears baked lanes left
// on replaced destinations, and re-bakes the macro lane into the new set on `clip`. Loaded
// destinations another macro also targets resolve by the usual ownership rank.
namespace Macros {
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex);
}
