#pragma once

#include <cstddef>
#include <new>

namespace test {

/// @brief A fallible allocator for specs: succeeds `fail_after(n)` times, then returns null until reset. Each
/// instantiation keeps its own count.
template <class T>
struct FailingAllocator {
	using value_type = T;

	inline static int remaining_ = -1; ///< -1: never fail
	inline static std::size_t live_ = 0;

	static void fail_after(int successes) { remaining_ = successes; }
	static void never_fail() { remaining_ = -1; }
	[[nodiscard]] static std::size_t live() { return live_; }

	constexpr FailingAllocator() noexcept = default;
	template <class U>
	constexpr FailingAllocator(const FailingAllocator<U>& /*other*/) noexcept {}

	[[nodiscard]] T* try_allocate(std::size_t n) noexcept {
		if (remaining_ == 0) {
			return nullptr;
		}
		if (remaining_ > 0) {
			--remaining_;
		}
		++live_;
		return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{alignof(T)}, std::nothrow));
	}
	void deallocate(T* p, std::size_t /*n*/) noexcept {
		--live_;
		::operator delete(p, std::align_val_t{alignof(T)});
	}
	template <class U>
	constexpr bool operator==(const FailingAllocator<U>& /*other*/) const noexcept {
		return true;
	}
};

} // namespace test
