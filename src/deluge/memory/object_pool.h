/*
 * Copyright © 2025 Katherine Whitlock
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
#include "libdeluge/system.h"
#include <cstring>
#include <gsl/gsl>
#include <memory>
#include <vector>

namespace deluge::memory {
/// @brief A simple object pool implementation
/// @tparam T The type of object to pool
/// @tparam Alloc The allocator to use for the pool
/// @note One global pool per object type, shared across execution tiers; the free-list
/// operations are guarded accordingly. See get() for why this is not thread_local.
template <typename T, template <typename> typename Alloc = std::allocator>
class ObjectPool {
	static constexpr size_t kDefaultSize = 48;
	ObjectPool() { repopulate(); };

public:
	/// @brief The type of object in the pool
	using value_type = T;

	~ObjectPool() { clear(); };

	/// @brief Gets the global pool for a given object
	/// @returns A reference to the pool
	///
	/// @warning Deliberately NOT `thread_local`. This is a single-core firmware with no TLS set
	/// up: nothing initialises the CP15 `TPIDRURO` thread-pointer register anywhere in the
	/// firmware or the BSP. A `thread_local` here made clang (whose ARM target feature set
	/// includes `+read-tp-tpidruro`) read that uninitialised register to locate the TLS block,
	/// yielding a wild pointer — observed as a boot-time synchronous external abort in
	/// `AudioEngine::init()` with DFAR=0xFB92600F. Plain `static` is also what the semantics
	/// actually want: one global pool per object type.
	static constexpr ObjectPool& get() {
		static ObjectPool instance;
		return instance;
	}

	/// @brief Sets the size of the pool
	/// @param size The new size of the pool
	void resize(size_t n) {
		capacity_ = n;
		if (capacity_ < objects_.size()) {
			// We need to shrink the pool
			while (objects_.size() > capacity_) {
				alloc_.deallocate(objects_.back(), 1);
				objects_.pop_back();
			}
		}
	}

	/// @brief Gets the capacity of the pool
	/// @returns The capacity of the pool
	[[nodiscard]] size_t capacity() const { return capacity_; }

	/// @brief Gets the number of objects in the pool
	/// @returns The number of objects in the pool
	[[nodiscard]] size_t size() const { return objects_.size(); }

	/// Recycles an object back into the pool
	/// @param obj The object to recycle
	static void recycle(gsl::owner<T*> obj) {
		obj->~T();

		// Destruction and any deallocation stay outside the guard; only the list touch is protected.
		if (!get().try_push(obj)) {
			Alloc<T>().deallocate(obj, 1);
		}
	}

	/// @brief Repopulates the pool to its original size
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	void repopulate() noexcept(false) {
		// Reserve before filling: this is what lets try_push() run inside a critical section without
		// ever reaching the heap, since the free list never exceeds `capacity_` entries.
		objects_.reserve(capacity_);
		for (size_t i = objects_.size(); i < capacity_; ++i) {
			objects_.push_back(alloc_.allocate(1));
		}
	}

	/// @brief A managed pointer type for an object from the pool
	using pointer_type = std::unique_ptr<T, decltype(&recycle)>;

	/// @brief Acquires an object from the pool
	/// @tparam Args The types of arguments to pass to the object's constructor
	/// @param args The arguments to pass to the object's constructor
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	/// @returns A managed pointer to the acquired object
	template <typename... Args>
	[[nodiscard]] pointer_type acquire(Args&&... args) noexcept(false) {
		T* obj = try_pop();
		if (obj == nullptr) {
			// The pool is empty, so fall back to the heap -- deliberately outside the guard.
			obj = alloc_.allocate(1);
		}
#ifdef DELUGE_DETERMINISTIC_ALLOC
		// A recycled slot still holds the previous occupant's bytes (recycle() runs the destructor but doesn't
		// clear), so zero it before reconstruction — the pool-reuse analogue of the allocator's zero-on-alloc, so a
		// read-before-write in T resolves to a defined 0 regardless of what was pooled here before. Sim/golden
		// builds only (see sim/CMakeLists DELUGE_DETERMINISTIC_ALLOC); firmware leaves it off.
		memset(static_cast<void*>(obj), 0, sizeof(T));
#endif
		return {new (obj) T(std::forward<Args>(args)...), &recycle};
	}

	/// @brief Clears the pool
	void clear() {
		while (!objects_.empty()) {
			alloc_.deallocate(objects_.back(), 1);
			objects_.pop_back();
		}
	}

	/// @brief Checks if the pool is empty
	/// @returns true if the pool is empty, false otherwise
	[[nodiscard]] constexpr bool empty() const { return objects_.empty(); }

private:
	/// @brief Takes a slot off the free list, or nullptr if it is empty
	///
	/// The free list is shared across execution tiers, and since audio renders from a preemptive
	/// interrupt executor it can interrupt a caller mid-update. `back()` followed by `pop_back()` is a
	/// read-then-mutate, so without this guard two racing acquirers could both read the same slot and
	/// hand one object to two owners -- observed on device as a note-on reaching assignClusters with a
	/// region still held (`i024`). A push arriving mid-read could also reallocate the vector underneath
	/// it.
	///
	/// Only the list operation is guarded. Allocation, deallocation and construction all stay outside,
	/// so the critical section is a couple of vector operations and never reaches the heap -- see
	/// repopulate(), which reserves `capacity_` so a push cannot grow the vector.
	T* try_pop() {
		CriticalSectionGuard guard;
		if (objects_.empty()) {
			return nullptr;
		}
		T* obj = objects_.back();
		objects_.pop_back();
		return obj;
	}

	/// @brief Returns a slot to the free list
	/// @returns false if the pool is already at capacity, so the caller must free the slot instead
	/// @note Guarded for the same reason as try_pop(), and likewise allocation-free.
	bool try_push(T* obj) {
		CriticalSectionGuard guard;
		if (objects_.size() >= capacity_) {
			return false;
		}
		objects_.push_back(obj);
		return true;
	}

	size_t capacity_ = kDefaultSize;
	std::vector<T*, Alloc<T*>> objects_;
	Alloc<T> alloc_;
};
} // namespace deluge::memory
