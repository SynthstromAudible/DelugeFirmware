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

namespace deluge::plugin {

/// @brief Which of an instrument's parameter collections a parameter id belongs to; with the id, it is how the host
/// and a plugin name one parameter.
enum class ParamKind : int32_t {
	NONE,
	/// Voice-local parameters which can be modulated via the mod matrix
	PATCHED,
	/// Parameters which apply to the internal sound engine specifically
	UNPATCHED_SOUND,
	/// Parameters which can not be modulated and apply to the whole Output
	UNPATCHED_GLOBAL,
	/// Unused
	STATIC,
	/// Internal representation for MIDI CCs
	MIDI,
	/// Modulates the strength of the modulation between a mod source and another param
	PATCH_CABLE,
	/// Parameter connected to an MPE expression axis
	EXPRESSION,
};

} // namespace deluge::plugin
