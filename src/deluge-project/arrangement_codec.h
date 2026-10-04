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

#include "deluge-project/arrangement.h"
#include "deluge-project/codec.h"
#include "serialization/value.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>

namespace deluge::project {

/// @brief The clips and outputs a pre-id file's session macros name by position, per macro slot. The model cannot
/// resolve a position, so decode reports them for the host to resolve once its clips and outputs exist.
struct LegacyMacroTargets {
	std::array<std::optional<int32_t>, 8> clip_index;
	std::array<std::optional<int32_t>, 8> output_index;
};

/// @brief The `sections` list: every section, with its launch command and the command's device.
[[nodiscard]] serialization::Value encode_sections(const Arrangement& arrangement);

/// @brief The `sessionMacros` list, up to the last macro that does something; empty when none does.
[[nodiscard]] serialization::Value encode_macros(const Arrangement& arrangement);

/// @brief Read the song's `sections` and `sessionMacros`. A macro names its clip and output by the id the file
/// holds (the host checks the song holds them); a positional target goes to @p legacy if given, keeping the macro.
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& song, Arrangement& arrangement,
                                                    LegacyMacroTargets* legacy = nullptr);

} // namespace deluge::project
