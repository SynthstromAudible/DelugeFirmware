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

// Every param's default as the engines' init sets it, recorded as the oracle for the table-driven init: the init
// reads the tables, so the check compares both against this record rather than against each other.
#pragma once

#include <array>
#include <cstdint>

namespace param_defaults_reference {

enum class Table : uint8_t { INSTRUMENT, GLOBAL_EFFECTABLE };

struct Default {
	Table table;
	uint32_t id;
	int32_t value;
};

// clang-format off
inline constexpr std::array<Default, 125> kDefaults{{
    {Table::INSTRUMENT, 0, 2147483647},
    {Table::INSTRUMENT, 1, 2147483647},
    {Table::INSTRUMENT, 2, 0},
    {Table::INSTRUMENT, 3, -2147483648},
    {Table::INSTRUMENT, 4, -2147483648},
    {Table::INSTRUMENT, 5, -2147483648},
    {Table::INSTRUMENT, 6, -2147483648},
    {Table::INSTRUMENT, 7, -2147483648},
    {Table::INSTRUMENT, 8, -2147483648},
    {Table::INSTRUMENT, 9, -2147483648},
    {Table::INSTRUMENT, 10, -2147483648},
    {Table::INSTRUMENT, 11, 0},
    {Table::INSTRUMENT, 12, -2147483648},
    {Table::INSTRUMENT, 13, 0},
    {Table::INSTRUMENT, 14, -23},
    {Table::INSTRUMENT, 15, 0},
    {Table::INSTRUMENT, 16, 0},
    {Table::INSTRUMENT, 17, -2147483648},
    {Table::INSTRUMENT, 18, -2147483648},
    {Table::INSTRUMENT, 19, 0},
    {Table::INSTRUMENT, 20, 0},
    {Table::INSTRUMENT, 21, 0},
    {Table::INSTRUMENT, 22, 0},
    {Table::INSTRUMENT, 23, 0},
    {Table::INSTRUMENT, 24, 0},
    {Table::INSTRUMENT, 25, 0},
    {Table::INSTRUMENT, 26, 0},
    {Table::INSTRUMENT, 27, 0},
    {Table::INSTRUMENT, 28, 0},
    {Table::INSTRUMENT, 29, 0},
    {Table::INSTRUMENT, 30, -2147483648},
    {Table::INSTRUMENT, 31, 0},
    {Table::INSTRUMENT, 32, 0},
    {Table::INSTRUMENT, 33, 0},
    {Table::INSTRUMENT, 34, -429496748},
    {Table::INSTRUMENT, 35, 0},
    {Table::INSTRUMENT, 36, 0},
    {Table::INSTRUMENT, 37, 0},
    {Table::INSTRUMENT, 38, -429496748},
    {Table::INSTRUMENT, 39, 0},
    {Table::INSTRUMENT, 40, 0},
    {Table::INSTRUMENT, 41, 0},
    {Table::INSTRUMENT, 42, -429496748},
    {Table::INSTRUMENT, 43, 0},
    {Table::INSTRUMENT, 44, 0},
    {Table::INSTRUMENT, 45, 1288490152},
    {Table::INSTRUMENT, 46, 0},
    {Table::INSTRUMENT, 47, -2147483648},
    {Table::INSTRUMENT, 48, 0},
    {Table::INSTRUMENT, 49, -2147483648},
    {Table::INSTRUMENT, 50, 0},
    {Table::INSTRUMENT, 51, 0},
    {Table::INSTRUMENT, 52, 429496702},
    {Table::INSTRUMENT, 53, 429496702},
    {Table::INSTRUMENT, 54, 0},
    {Table::INSTRUMENT, 90, 0},
    {Table::INSTRUMENT, 91, 0},
    {Table::INSTRUMENT, 92, 0},
    {Table::INSTRUMENT, 93, 0},
    {Table::INSTRUMENT, 94, 0},
    {Table::INSTRUMENT, 95, -2147483648},
    {Table::INSTRUMENT, 96, -2147483648},
    {Table::INSTRUMENT, 97, 0},
    {Table::INSTRUMENT, 98, 0},
    {Table::INSTRUMENT, 99, -601295438},
    {Table::INSTRUMENT, 100, 0},
    {Table::INSTRUMENT, 101, 0},
    {Table::INSTRUMENT, 102, -2147483648},
    {Table::INSTRUMENT, 103, -2147483648},
    {Table::INSTRUMENT, 104, -2147483648},
    {Table::INSTRUMENT, 105, -2147483648},
    {Table::INSTRUMENT, 106, 2147483647},
    {Table::INSTRUMENT, 107, -2147483648},
    {Table::INSTRUMENT, 108, -2147483648},
    {Table::INSTRUMENT, 109, -2147483648},
    {Table::INSTRUMENT, 110, -2147483648},
    {Table::INSTRUMENT, 111, -2147483648},
    {Table::INSTRUMENT, 112, -2147483648},
    {Table::INSTRUMENT, 113, -2147483648},
    {Table::INSTRUMENT, 114, -2147483648},
    {Table::INSTRUMENT, 115, -2147483648},
    {Table::INSTRUMENT, 116, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 90, 0},
    {Table::GLOBAL_EFFECTABLE, 91, 0},
    {Table::GLOBAL_EFFECTABLE, 92, 0},
    {Table::GLOBAL_EFFECTABLE, 93, 0},
    {Table::GLOBAL_EFFECTABLE, 94, 0},
    {Table::GLOBAL_EFFECTABLE, 95, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 96, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 97, 0},
    {Table::GLOBAL_EFFECTABLE, 98, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 99, -601295438},
    {Table::GLOBAL_EFFECTABLE, 100, 0},
    {Table::GLOBAL_EFFECTABLE, 101, 2147483647},
    {Table::GLOBAL_EFFECTABLE, 102, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 103, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 104, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 105, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 106, 2147483647},
    {Table::GLOBAL_EFFECTABLE, 107, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 108, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 109, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 110, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 111, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 112, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 113, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 114, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 115, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 116, -536870912},
    {Table::GLOBAL_EFFECTABLE, 117, 0},
    {Table::GLOBAL_EFFECTABLE, 118, 0},
    {Table::GLOBAL_EFFECTABLE, 119, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 120, 0},
    {Table::GLOBAL_EFFECTABLE, 121, 0},
    {Table::GLOBAL_EFFECTABLE, 122, 2147483647},
    {Table::GLOBAL_EFFECTABLE, 123, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 124, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 125, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 126, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 127, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 128, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 129, 0},
    {Table::GLOBAL_EFFECTABLE, 130, -2147483648},
    {Table::GLOBAL_EFFECTABLE, 131, 0},
    {Table::GLOBAL_EFFECTABLE, 132, 0},
}};
// clang-format on

} // namespace param_defaults_reference
