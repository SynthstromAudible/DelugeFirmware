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
#include <array>
#include <cstddef>
#include <cstdint>
#include <gsl/gsl>
#include <memory>

namespace deluge::memory {
/// @brief A simple object pool implementation
/// @tparam T The type of object to pool
/// @tparam Alloc The allocator to use for the pool
/// @note ObjectPool instances are thread-local singletons
///
/// When the pool needs more than one object at once (on construction, and in repopulate()), it allocates them as a
/// single block - a "slab" - rather than one allocation each. The general allocator rounds each allocation up to a
/// power of two including its header, so a Voice (just over 2 KiB) used to take a 4 KiB block; packed into a slab, 48
/// Voices take ~98 KiB instead of 192 KiB. Slab objects can't be freed individually, so they always return to the
/// pool. Objects allocated one at a time once the pool runs dry ("overflow" objects) are kept only while the pool is
/// below capacity, and freed otherwise.
template <typename T, template <typename> typename Alloc = std::allocator>
class ObjectPool {
	static constexpr size_t kDefaultSize = 48;
	/// Each repopulate() that needs more than one object uses a slab. Pools are repopulated once at startup, so this
	/// only needs to cover a few later grows; past it, repopulate() falls back to individual allocations.
	static constexpr size_t kMaxSlabs = 4;

	// Free objects have been destroyed, so their storage holds the free-list link.
	struct FreeNode {
		FreeNode* next;
	};

	struct Slab {
		T* begin = nullptr;
		size_t count = 0;
		[[nodiscard]] bool contains(const T* obj) const {
			auto addr = reinterpret_cast<uintptr_t>(obj);
			auto start = reinterpret_cast<uintptr_t>(begin);
			return addr >= start && addr < start + count * sizeof(T);
		}
	};

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
	/// @note Shrinking frees idle overflow objects. Slab memory can only be released whole, so it's released (and the
	/// pool repopulated as one smaller slab) only if no slab object is in use.
	void resize(size_t n) {
		capacity_ = n;
		while (size() > capacity_ && overflow_free_ != nullptr) {
			alloc_.deallocate(pop(overflow_free_, num_overflow_free_), 1);
		}
		if (size() > capacity_ && num_slab_free_ == slabObjectCount()) {
			releaseSlabs();
			repopulate();
		}
	}

	/// @brief Gets the capacity of the pool
	/// @returns The capacity of the pool
	[[nodiscard]] size_t capacity() const { return capacity_; }

	/// @brief Gets the number of objects in the pool
	/// @returns The number of objects in the pool
	[[nodiscard]] size_t size() const { return num_slab_free_ + num_overflow_free_; }

	/// Recycles an object back into the pool
	/// @param obj The object to recycle
	static void recycle(gsl::owner<T*> obj) {
		obj->~T();
		get().put(obj);
	}

	/// @brief Repopulates the pool to its original size
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	void repopulate() noexcept(false) {
		if (size() >= capacity_) {
			return;
		}
		size_t needed = capacity_ - size();
		if (needed > 1 && num_slabs_ < kMaxSlabs) {
			T* block = alloc_.allocate(needed);
			slabs_[num_slabs_++] = {block, needed};
			for (size_t i = needed; i-- > 0;) {
				push(slab_free_, num_slab_free_, &block[i]);
			}
			return;
		}
		for (; needed > 0; --needed) {
			push(overflow_free_, num_overflow_free_, alloc_.allocate(1));
		}
	}

	/// @brief A managed pointer type for an object from the pool
	/// @note The deleter is a function pointer rather than a stateless functor so that code destroying a pointer_type
	/// (e.g. Sound's inline destructor) doesn't need T to be complete.
	using pointer_type = std::unique_ptr<T, decltype(&recycle)>;

	/// @brief Acquires an object from the pool
	/// @tparam Args The types of arguments to pass to the object's constructor
	/// @param args The arguments to pass to the object's constructor
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	/// @returns A managed pointer to the acquired object
	template <typename... Args>
	[[nodiscard]] pointer_type acquire(Args&&... args) noexcept(false) {
		T* obj;
		// Prefer slab objects, so idle overflow objects can be freed as the pool refills
		if (slab_free_ != nullptr) {
			obj = pop(slab_free_, num_slab_free_);
		}
		else if (overflow_free_ != nullptr) {
			obj = pop(overflow_free_, num_overflow_free_);
		}
		else {
			obj = alloc_.allocate(1);
		}
		return {new (obj) T(std::forward<Args>(args)...), &recycle};
	}

	/// @brief Clears the pool
	/// @note Slabs with objects still in use stay allocated; those objects return to the pool when recycled.
	void clear() {
		while (overflow_free_ != nullptr) {
			alloc_.deallocate(pop(overflow_free_, num_overflow_free_), 1);
		}
		if (num_slab_free_ == slabObjectCount()) {
			releaseSlabs();
		}
	}

	/// @brief Checks if the pool is empty
	/// @returns true if the pool is empty, false otherwise
	[[nodiscard]] constexpr bool empty() const { return size() == 0; }

private:
	static void push(FreeNode*& list, size_t& count, T* obj) {
		// Checked here rather than at class scope, so the pool can be named while T is incomplete
		static_assert(sizeof(T) >= sizeof(FreeNode) && alignof(T) >= alignof(FreeNode),
		              "pooled objects must be able to hold a free-list link");
		auto* node = reinterpret_cast<FreeNode*>(obj);
		node->next = list;
		list = node;
		++count;
	}

	static T* pop(FreeNode*& list, size_t& count) {
		FreeNode* node = list;
		list = node->next;
		--count;
		return reinterpret_cast<T*>(node);
	}

	[[nodiscard]] bool isSlabObject(const T* obj) const {
		for (size_t i = 0; i < num_slabs_; ++i) {
			if (slabs_[i].contains(obj)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] size_t slabObjectCount() const {
		size_t total = 0;
		for (size_t i = 0; i < num_slabs_; ++i) {
			total += slabs_[i].count;
		}
		return total;
	}

	void put(T* obj) {
		if (isSlabObject(obj)) {
			push(slab_free_, num_slab_free_, obj);
			// Slab objects can't be freed, so make room by freeing an idle overflow object instead
			if (size() > capacity_ && overflow_free_ != nullptr) {
				alloc_.deallocate(pop(overflow_free_, num_overflow_free_), 1);
			}
		}
		else if (size() < capacity_) {
			push(overflow_free_, num_overflow_free_, obj);
		}
		else {
			alloc_.deallocate(obj, 1);
		}
	}

	// Only call when every slab object is free
	void releaseSlabs() {
		slab_free_ = nullptr;
		num_slab_free_ = 0;
		for (size_t i = 0; i < num_slabs_; ++i) {
			alloc_.deallocate(slabs_[i].begin, slabs_[i].count);
			slabs_[i] = {};
		}
		num_slabs_ = 0;
	}

	size_t capacity_ = kDefaultSize;
	FreeNode* slab_free_ = nullptr;
	FreeNode* overflow_free_ = nullptr;
	size_t num_slab_free_ = 0;
	size_t num_overflow_free_ = 0;
	std::array<Slab, kMaxSlabs> slabs_{};
	size_t num_slabs_ = 0;
	Alloc<T> alloc_;
};
} // namespace deluge::memory
