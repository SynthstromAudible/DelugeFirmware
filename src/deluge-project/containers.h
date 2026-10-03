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

#include "deluge-project/model_memory.h"

#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace deluge::project {

/// @brief An ordered map whose memory comes from the host's model hooks.
template <class Key, class T, class Compare = std::less<Key>>
using model_map = std::map<Key, T, Compare, ModelAllocator<std::pair<const Key, T>>>;

/// @brief A vector whose memory comes from the host's model hooks.
template <class T>
using model_vector = std::vector<T, ModelAllocator<T>>;

/// @brief Why a fallible model-container operation failed.
enum class AllocError : uint8_t {
	OUT_OF_MEMORY, ///< the host could not supply the memory (or an element's construction failed)
};

} // namespace deluge::project
