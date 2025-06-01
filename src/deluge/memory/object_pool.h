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
#include <gsl/gsl>
#include <memory>
#include <stack>
#include <vector>

namespace deluge::memory {
/// @brief A simple object pool implementation
/// @tparam T The type of object to pool
/// @tparam Alloc The allocator to use for the pool
/// @note ObjectPool instances are thread-local singletons
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
	static constexpr ObjectPool& get() {
		static thread_local ObjectPool instance;
		return instance;
	}

	/// @brief Sets the size of the pool
	/// @param size The new size of the pool
	void resize(size_t n) {
		capacity_ = n;
		if (capacity_ < objects_.size()) {
			// We need to shrink the pool
			while (objects_.size() > capacity_) {
				alloc_.deallocate(objects_.top(), 1);
				objects_.pop();
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

		ObjectPool& op = get();
		if (op.objects_.size() < op.capacity_) {
			op.objects_.push(obj);
		}
		else {
			Alloc<T>().deallocate(obj, 1);
		}
	}

	/// @brief Repopulates the pool to its original size
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	void repopulate() noexcept(false) {
		for (size_t i = objects_.size(); i < capacity_; ++i) {
			objects_.push(alloc_.allocate(1));
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
		T* obj = nullptr;
		if (objects_.empty()) {
			obj = alloc_.allocate(1);
		}
		else {
			obj = objects_.top();
			objects_.pop();
		}
		return {new (obj) T(std::forward<Args>(args)...), &recycle};
	}

	/// @brief Clears the pool
	void clear() {
		while (!objects_.empty()) {
			alloc_.deallocate(objects_.top(), 1);
			objects_.pop();
		}
	}

	/// @brief Checks if the pool is empty
	/// @returns true if the pool is empty, false otherwise
	[[nodiscard]] constexpr bool empty() const { return objects_.empty(); }

private:
	size_t capacity_ = kDefaultSize;
	std::stack<T*, std::vector<T*, Alloc<T*>>> objects_;
	Alloc<T> alloc_;
};
} // namespace deluge::memory
