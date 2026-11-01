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

#include <cstdint>

/// @brief An exponential-family param's final value: the release-rate table for envelope decays and releases, the
/// inverted exponential for envelope attacks, the exponential for the rest.
/// @param paramNeutralValue The param's neutral value.
/// @param patchedValue The combination, Q29.
/// @param p The patched param.
/// @return The final value.
int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int32_t p);
