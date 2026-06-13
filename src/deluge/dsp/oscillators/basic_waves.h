/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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
#include <utility>
namespace deluge::dsp {
/**
 * @brief Get a table number and size, depending on the increment
 *
 * @return table_number, table_size
 */
std::pair<int32_t, int32_t> getTableNumber(uint32_t phaseIncrement);
extern const int16_t* sawTables[20];
extern const int16_t* squareTables[20];
extern const int16_t* analogSquareTables[20];
extern const int16_t* analogSawTables[20];

} // namespace deluge::dsp
