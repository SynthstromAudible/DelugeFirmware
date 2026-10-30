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

#include "plugin-api/types/param_naming.h"

namespace deluge::instrument {

/// @brief How files name the Deluge instrument's params: a param by its file name, a cable's depth by its param and
/// its sources (nearest the param first). A mod knob pointed at a volume points at the one the runtime uses: a plain
/// volume knob at `volumePostFX`, a knob on a sidechain cable's depth at `volumePostReverbSend`, a knob on another
/// cable's depth at `volume`.
extern const plugin::ParamNaming kNaming;

} // namespace deluge::instrument
