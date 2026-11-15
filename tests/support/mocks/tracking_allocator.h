#pragma once

#include <algorithm>
#include <set>

template <typename T>
class TrackingAllocator {
	inline static std::set<T*> allocated_{};
	inline static std::set<T*> deallocated_{};

public:
	using allocator_type = std::allocator<T>;
	using value_type = T;

	TrackingAllocator() noexcept = default;
	TrackingAllocator(const TrackingAllocator&) noexcept = default;

	template <typename U>
	TrackingAllocator(const TrackingAllocator<U>&) noexcept {}

	[[nodiscard]] value_type* allocate(std::size_t n) {
		auto* ptr = allocator_type().allocate(n);
		TrackingAllocator::allocated_.insert(ptr);
		return ptr;
	}

	void deallocate(value_type* ptr, std::size_t n) {
		allocator_type().deallocate(ptr, n);
		TrackingAllocator::deallocated_.insert(ptr);
	}

	[[nodiscard]] static std::set<T*> allocated() noexcept { return allocated_; }
	[[nodiscard]] static std::set<T*> deallocated() noexcept { return deallocated_; }
	[[nodiscard]] static std::set<T*> outstanding() noexcept {
		std::set<T*> leaked;
		for (const auto& ptr : allocated_) {
			if (!deallocated_.contains(ptr)) {
				leaked.insert(ptr);
			}
		}
		return leaked;
	}

	[[nodiscard]] static std::size_t num_allocated() noexcept { return allocated_.size(); }
	[[nodiscard]] static std::size_t num_deallocated() noexcept { return deallocated_.size(); }
	[[nodiscard]] static std::size_t num_outstanding() noexcept { return outstanding().size(); }

	[[nodiscard]] static bool is_allocated(value_type* ptr) noexcept {
		return allocated_.contains(ptr) && !deallocated_.contains(ptr);
	}

	[[nodiscard]] static bool is_deallocated(value_type* ptr) noexcept { return deallocated_.contains(ptr); }

	static void reset() noexcept {
		allocated_.clear();
		deallocated_.clear();
	}
};
