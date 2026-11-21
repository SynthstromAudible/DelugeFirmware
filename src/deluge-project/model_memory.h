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

#include "foundation/oom.h"

#include <cstddef>
#include <limits>

/// @brief Where the model's memory comes from, supplied by the host at link time: the device gives the model the
/// general heap its containers draw on, a host tool the ordinary heap.
/// @param bytes     Size of the request in bytes.
/// @param alignment Required alignment in bytes.
/// @return The memory, or nullptr when the request cannot be met.
extern "C" void* deluge_model_alloc(std::size_t bytes, std::size_t alignment) noexcept;

/// @brief Return memory from deluge_model_alloc made with this @p alignment.
/// @param p         Memory returned by deluge_model_alloc.
/// @param alignment The alignment passed to the matching deluge_model_alloc.
extern "C" void deluge_model_free(void* p, std::size_t alignment) noexcept;

namespace deluge::project {

/// @brief The allocator of every model container: stateless, forwarding to the host's hooks.
template <class T>
struct ModelAllocator {
	using value_type = T;

	constexpr ModelAllocator() noexcept = default;
	template <class U>
	constexpr ModelAllocator(const ModelAllocator<U>& /*other*/) noexcept {}

	/// @brief The host's memory for @p n objects, or null when it has none.
	/// @param n Element count; must be > 0.
	/// @return The memory, or nullptr.
	[[nodiscard]] T* try_allocate(std::size_t n) noexcept {
		if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
			return nullptr;
		}
		return static_cast<T*>(deluge_model_alloc(n * sizeof(T), alignof(T)));
	}

	[[nodiscard]] T* allocate(std::size_t n) {
		if (n == 0) {
			return nullptr;
		}
		T* p = try_allocate(n);
		if (p == nullptr) [[unlikely]] {
			deluge::oom();
		}
		return p;
	}

	void deallocate(T* p, std::size_t /*n*/) noexcept { deluge_model_free(p, alignof(T)); }

	template <class U>
	constexpr bool operator==(const ModelAllocator<U>& /*other*/) const noexcept {
		return true;
	}
};

} // namespace deluge::project
