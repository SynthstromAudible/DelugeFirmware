#pragma once

#include "foundation/alloc_error.h"
#include "foundation/fallible_allocator.h"
#include "foundation/oom.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace deluge {

/// @brief A contiguous vector whose growth can fail without exceptions.
///
/// The `try_*` members return AllocError and leave the vector exactly as it was; the plain members (`push_back`,
/// `reserve`, …) call oom() instead. Copying is explicit (`try_clone()` / `clone()`), so no deep copy happens by
/// accident. Growth matches libstdc++'s (size + max(size, n), exact on reserve), so swapping a std::vector for this
/// leaves the heap's layout as it was.
///
/// @tparam T     Element type; nothrow move-constructible and nothrow-destructible (element construction must not
///               throw: in the firmware nothing can).
/// @tparam Alloc A FallibleAllocator for T.
template <class T, class Alloc>
requires FallibleAllocator<Alloc, T> && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>
class TryVector {
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = T*;
	using const_iterator = const T*;
	using allocator_type = Alloc;

	TryVector() noexcept = default;
	TryVector(const TryVector&) = delete;
	TryVector& operator=(const TryVector&) = delete;
	TryVector(TryVector&& other) noexcept
	    : data_{std::exchange(other.data_, nullptr)}, size_{std::exchange(other.size_, 0)},
	      capacity_{std::exchange(other.capacity_, 0)} {}
	TryVector& operator=(TryVector&& other) noexcept {
		if (this != &other) {
			release();
			data_ = std::exchange(other.data_, nullptr);
			size_ = std::exchange(other.size_, 0);
			capacity_ = std::exchange(other.capacity_, 0);
		}
		return *this;
	}
	~TryVector() { release(); }

	// Observers
	[[nodiscard]] size_type size() const noexcept { return size_; }
	[[nodiscard]] size_type capacity() const noexcept { return capacity_; }
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] T* data() noexcept { return data_; }
	[[nodiscard]] const T* data() const noexcept { return data_; }
	[[nodiscard]] iterator begin() noexcept { return data_; }
	[[nodiscard]] iterator end() noexcept { return data_ + size_; }
	[[nodiscard]] const_iterator begin() const noexcept { return data_; }
	[[nodiscard]] const_iterator end() const noexcept { return data_ + size_; }
	[[nodiscard]] const_iterator cbegin() const noexcept { return data_; }
	[[nodiscard]] const_iterator cend() const noexcept { return data_ + size_; }
	[[nodiscard]] T& operator[](size_type i) noexcept { return data_[i]; }
	[[nodiscard]] const T& operator[](size_type i) const noexcept { return data_[i]; }
	[[nodiscard]] T& front() noexcept { return data_[0]; }
	[[nodiscard]] const T& front() const noexcept { return data_[0]; }
	[[nodiscard]] T& back() noexcept { return data_[size_ - 1]; }
	[[nodiscard]] const T& back() const noexcept { return data_[size_ - 1]; }

	// Non-allocating modifiers
	void clear() noexcept {
		std::destroy(begin(), end());
		size_ = 0;
	}
	void pop_back() noexcept { std::destroy_at(data_ + --size_); }
	iterator erase(const_iterator pos) noexcept { return erase(pos, pos + 1); }
	iterator erase(const_iterator first, const_iterator last) noexcept {
		iterator f = mut(first);
		iterator l = mut(last);
		if (f == l) {
			return f; // moving the tail onto itself would self-move-assign every element after it
		}
		iterator newEnd = std::move(l, end(), f);
		std::destroy(newEnd, end());
		size_ = static_cast<size_type>(newEnd - data_);
		return f;
	}
	/// @brief Give back the room past size(): a request, as std::vector's is. With no memory for a smaller buffer the
	///        room is kept.
	void shrink_to_fit() noexcept {
		if (size_ == capacity_) {
			return;
		}
		if (size_ == 0) {
			release();
			return;
		}
		T* fresh = alloc_.try_allocate(size_);
		if (fresh == nullptr) {
			return;
		}
		relocate_around(fresh, size_, 0);
		adopt(fresh, size_, size_);
	}
	void swap(TryVector& other) noexcept {
		std::swap(data_, other.data_);
		std::swap(size_, other.size_);
		std::swap(capacity_, other.capacity_);
	}

	// Fallible modifiers: on error the vector is unchanged
	[[nodiscard]] AllocResult<void> try_reserve(size_type n) {
		if (n <= capacity_) {
			return {};
		}
		return reallocate(n);
	}

	[[nodiscard]] AllocResult<void> try_push_back(const T& value) { return try_emplace_back(value).transform(drop); }
	[[nodiscard]] AllocResult<void> try_push_back(T&& value) {
		return try_emplace_back(std::move(value)).transform(drop);
	}

	template <class... Args>
	[[nodiscard]] AllocResult<T*> try_emplace_back(Args&&... args) {
		return try_emplace(end(), std::forward<Args>(args)...).transform([](iterator it) { return it; });
	}

	template <class... Args>
	[[nodiscard]] AllocResult<iterator> try_emplace(const_iterator pos, Args&&... args) {
		const auto index = static_cast<size_type>(pos - data_);
		if (size_ < capacity_) {
			if (index == size_) {
				std::construct_at(data_ + size_, std::forward<Args>(args)...);
			}
			else {
				T value(std::forward<Args>(args)...); // before shifting: args may refer into this vector
				open_gap(index, 1);
				data_[index] = std::move(value);
			}
			++size_;
			return data_ + index;
		}
		const size_type newCapacity = grown(1);
		T* fresh = alloc_.try_allocate(newCapacity);
		if (fresh == nullptr) {
			return std::unexpected(AllocError::OUT_OF_MEMORY);
		}
		std::construct_at(fresh + index, std::forward<Args>(args)...); // before moving: args may refer into this vector
		relocate_around(fresh, index, 1);
		adopt(fresh, newCapacity, size_ + 1);
		return data_ + index;
	}

	[[nodiscard]] AllocResult<iterator> try_insert(const_iterator pos, const T& value) {
		return try_emplace(pos, value);
	}
	[[nodiscard]] AllocResult<iterator> try_insert(const_iterator pos, T&& value) {
		return try_emplace(pos, std::move(value));
	}

	[[nodiscard]] AllocResult<iterator> try_insert(const_iterator pos, size_type count, const T& value)
	requires std::is_copy_constructible_v<T>
	{
		const auto index = static_cast<size_type>(pos - data_);
		if (count == 0) {
			return data_ + index;
		}
		const T copy(value); // value may refer into this vector
		if (size_ + count <= capacity_) {
			open_gap(index, count);
			// The gap's slots below the old end hold moved-from objects (assign); those past it are raw (construct).
			const size_type liveEnd = std::min(size_, index + count);
			std::fill(data_ + index, data_ + liveEnd, copy);
			std::uninitialized_fill(data_ + liveEnd, data_ + index + count, copy);
			size_ += count;
			return data_ + index;
		}
		const size_type newCapacity = grown(count);
		T* fresh = alloc_.try_allocate(newCapacity);
		if (fresh == nullptr) {
			return std::unexpected(AllocError::OUT_OF_MEMORY);
		}
		std::uninitialized_fill_n(fresh + index, count, copy);
		relocate_around(fresh, index, count);
		adopt(fresh, newCapacity, size_ + count);
		return data_ + index;
	}

	[[nodiscard]] AllocResult<void> try_resize(size_type n)
	requires std::is_default_constructible_v<T>
	{
		if (n <= size_) {
			std::destroy(data_ + n, end());
			size_ = n;
			return {};
		}
		if (n > capacity_) {
			if (auto r = reallocate(std::max(n, grown(n - size_))); !r) {
				return r;
			}
		}
		std::uninitialized_value_construct(data_ + size_, data_ + n);
		size_ = n;
		return {};
	}

	template <std::forward_iterator I>
	[[nodiscard]] AllocResult<void> try_assign(I first, I last) {
		const auto n = static_cast<size_type>(std::distance(first, last));
		if (n > capacity_) {
			T* fresh = alloc_.try_allocate(n);
			if (fresh == nullptr) {
				return std::unexpected(AllocError::OUT_OF_MEMORY);
			}
			std::uninitialized_copy(first, last, fresh);
			release();
			data_ = fresh;
			capacity_ = n;
			size_ = n;
			return {};
		}
		clear();
		std::uninitialized_copy(first, last, data_);
		size_ = n;
		return {};
	}

	[[nodiscard]] AllocResult<TryVector> try_clone() const
	requires std::is_copy_constructible_v<T>
	{
		TryVector copy;
		if (auto r = copy.try_assign(begin(), end()); !r) {
			return std::unexpected(r.error());
		}
		return copy;
	}

	// Infallible modifiers: oom() on failure
	void reserve(size_type n) { or_oom(try_reserve(n)); }
	void push_back(const T& value) { or_oom(try_push_back(value)); }
	void push_back(T&& value) { or_oom(try_push_back(std::move(value))); }
	template <class... Args>
	T& emplace_back(Args&&... args) {
		return *or_oom(try_emplace_back(std::forward<Args>(args)...));
	}
	template <class... Args>
	iterator emplace(const_iterator pos, Args&&... args) {
		return or_oom(try_emplace(pos, std::forward<Args>(args)...));
	}
	iterator insert(const_iterator pos, const T& value) { return or_oom(try_insert(pos, value)); }
	iterator insert(const_iterator pos, T&& value) { return or_oom(try_insert(pos, std::move(value))); }
	iterator insert(const_iterator pos, size_type count, const T& value) {
		return or_oom(try_insert(pos, count, value));
	}
	void resize(size_type n) { or_oom(try_resize(n)); }
	template <std::forward_iterator I>
	void assign(I first, I last) {
		or_oom(try_assign(first, last));
	}
	[[nodiscard]] TryVector clone() const { return or_oom(try_clone()); }

private:
	static constexpr auto drop = [](auto&&) {};

	[[nodiscard]] iterator mut(const_iterator it) noexcept { return data_ + (it - data_); }

	/// libstdc++'s _M_check_len: size + max(size, n).
	[[nodiscard]] size_type grown(size_type n) const noexcept { return size_ + std::max(size_, n); }

	/// Move [index, size) up by @p count within capacity; slots [index, index+count) are left holding moved-from
	/// objects where they overlap live ones, and raw storage past the old end.
	void open_gap(size_type index, size_type count) noexcept {
		T* oldEnd = end();
		T* split = std::max(data_ + index, oldEnd - static_cast<difference_type>(std::min(count, size_ - index)));
		std::uninitialized_move(split, oldEnd, split + count);
		std::move_backward(data_ + index, split, split + count);
	}

	/// Move every element into @p fresh, leaving a @p count gap at @p index; destroys the moved-from originals.
	void relocate_around(T* fresh, size_type index, size_type count) noexcept {
		std::uninitialized_move(data_, data_ + index, fresh);
		std::uninitialized_move(data_ + index, end(), fresh + index + count);
		std::destroy(begin(), end());
	}

	void adopt(T* fresh, size_type newCapacity, size_type newSize) noexcept {
		if (data_ != nullptr) {
			alloc_.deallocate(data_, capacity_);
		}
		data_ = fresh;
		capacity_ = newCapacity;
		size_ = newSize;
	}

	[[nodiscard]] AllocResult<void> reallocate(size_type newCapacity) {
		T* fresh = alloc_.try_allocate(newCapacity);
		if (fresh == nullptr) {
			return std::unexpected(AllocError::OUT_OF_MEMORY);
		}
		relocate_around(fresh, size_, 0);
		adopt(fresh, newCapacity, size_);
		return {};
	}

	void release() noexcept {
		clear();
		if (data_ != nullptr) {
			alloc_.deallocate(data_, capacity_);
			data_ = nullptr;
			capacity_ = 0;
		}
	}

	T* data_ = nullptr;
	size_type size_ = 0;
	size_type capacity_ = 0;
	[[no_unique_address]] Alloc alloc_{};
};

/// @brief Erase every element @p pred holds for, keeping the rest in order (C++20's uniform container erasure).
/// @return The number erased.
template <class T, class Alloc, class Pred>
typename TryVector<T, Alloc>::size_type erase_if(TryVector<T, Alloc>& v, Pred pred) {
	auto kept = std::remove_if(v.begin(), v.end(), pred);
	const auto erased = static_cast<typename TryVector<T, Alloc>::size_type>(v.end() - kept);
	v.erase(kept, v.end());
	return erased;
}

/// @brief Erase every element equal to @p value, keeping the rest in order (C++20's uniform container erasure).
/// @return The number erased.
template <class T, class Alloc, class U>
typename TryVector<T, Alloc>::size_type erase(TryVector<T, Alloc>& v, const U& value) {
	return erase_if(v, [&](const T& element) { return element == value; });
}

} // namespace deluge
