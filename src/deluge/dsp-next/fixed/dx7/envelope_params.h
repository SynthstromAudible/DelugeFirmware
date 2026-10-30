/*
 * Derived from Dexed's env, under its licence:
 *
 * Copyright 2017 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
/// @brief The eight patch bytes of a DX7 envelope: four rates, then four levels.
namespace deluge::dsp::fixed::dx7 {

/// @brief An envelope's patch bytes as the patch lays them out: rates 1 to 4, then levels 1 to 4, each 0 to 99.
struct EnvelopeParams {
	uint8_t rates[4];  // NOLINT(*-avoid-c-arrays): the patch's 8 bytes, as laid out
	uint8_t levels[4]; // NOLINT(*-avoid-c-arrays)
};
static_assert(sizeof(EnvelopeParams) == 8);

} // namespace deluge::dsp::fixed::dx7
