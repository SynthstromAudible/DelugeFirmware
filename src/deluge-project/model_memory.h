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

#include <cstddef>

/// @brief Where the model's memory comes from, supplied by the host at link time: the device gives the model the heap
/// its containers have always used, a host tool the ordinary heap.
/// @return The memory, or nullptr when the request cannot be met.
extern "C" void* deluge_model_alloc(std::size_t bytes, std::size_t alignment) noexcept;

/// @brief Return memory from deluge_model_alloc made with this @p alignment.
extern "C" void deluge_model_free(void* p, std::size_t alignment) noexcept;

namespace deluge::project {

/// @brief Raise the host's out-of-memory exception; supplied by the host at link time, so a model container's failed
/// allocation reaches the same handlers as the rest of the host's.
[[noreturn]] void model_out_of_memory();

/// @brief The allocator of every model container: stateless, forwarding to the host's hooks.
template <class T>
struct ModelAllocator {
	using value_type = T;

	constexpr ModelAllocator() noexcept = default;
	template <class U>
	constexpr ModelAllocator(const ModelAllocator<U>& /*other*/) noexcept {}

	[[nodiscard]] T* allocate(std::size_t n) {
		if (n == 0) {
			return nullptr;
		}
		void* p = deluge_model_alloc(n * sizeof(T), alignof(T));
		if (p == nullptr) [[unlikely]] {
			model_out_of_memory();
		}
		return static_cast<T*>(p);
	}

	void deallocate(T* p, std::size_t /*n*/) noexcept { deluge_model_free(p, alignof(T)); }

	template <class U>
	constexpr bool operator==(const ModelAllocator<U>& /*other*/) const noexcept {
		return true;
	}
};

} // namespace deluge::project
