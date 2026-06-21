/*
 * Copyright © 2015-2024 Synthstrom Audible Limited
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

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

/// One entry in the allocator's free-list: a run of empty memory.
///
/// Members are declared (length, address) so the defaulted three-way comparison yields exactly the
/// allocator's required ordering — ascending by length, then by address. `length` and `address` are
/// both raw byte counts / addresses with no fixed-point encoding.
struct EmptySpaceRecord {
	uint32_t length;
	uint32_t address;
	// Compares members in declaration order → (length, address) ascending. This *is* the sort key.
	friend auto operator<=>(const EmptySpaceRecord&, const EmptySpaceRecord&) = default;
	friend bool operator==(const EmptySpaceRecord&, const EmptySpaceRecord&) = default;
};

/// A non-allocating, never-throwing fixed-capacity sorted container of `EmptySpaceRecord`, kept
/// ascending by (length, address) — essentially a `std::flat_multiset<EmptySpaceRecord>` over an
/// externally-owned buffer.
///
/// It lives *inside* the memory allocator, so it must never allocate through the GMA and must never
/// throw. A full buffer is reported idiomatically: `insert` returns `{end(), false}` rather than
/// throwing or reallocating; the caller copes (the allocator logs "Lost track of empty space").
///
/// The backing storage is adopted via `reset()` (two-phase, because each memory region's static
/// buffer is only known at `setup()` time). `EmptySpaceRecord` is trivially copyable, so the
/// insert/erase shifts are plain `memmove`-cost moves.
class EmptySpaceVector {
public:
	using value_type = EmptySpaceRecord;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = EmptySpaceRecord&;
	using const_reference = const EmptySpaceRecord&;
	using iterator = EmptySpaceRecord*; // contiguous / random-access
	using const_iterator = const EmptySpaceRecord*;

	EmptySpaceVector() = default;

	/// Adopt an external fixed-capacity buffer and reset to empty.
	void reset(std::span<EmptySpaceRecord> storage) noexcept {
		storage_ = storage;
		size_ = 0;
	}

	// capacity / observers
	[[nodiscard]] size_type size() const noexcept { return size_; }
	[[nodiscard]] size_type capacity() const noexcept { return storage_.size(); }
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] bool full() const noexcept { return size_ == storage_.size(); }
	void clear() noexcept { size_ = 0; }

	// element access
	reference operator[](size_type i) noexcept { return storage_[i]; }
	const_reference operator[](size_type i) const noexcept { return storage_[i]; }
	reference front() noexcept { return storage_[0]; }
	const_reference front() const noexcept { return storage_[0]; }
	reference back() noexcept { return storage_[size_ - 1]; }
	const_reference back() const noexcept { return storage_[size_ - 1]; }
	EmptySpaceRecord* data() noexcept { return storage_.data(); }
	const EmptySpaceRecord* data() const noexcept { return storage_.data(); }

	// iterators
	iterator begin() noexcept { return storage_.data(); }
	iterator end() noexcept { return storage_.data() + size_; }
	const_iterator begin() const noexcept { return storage_.data(); }
	const_iterator end() const noexcept { return storage_.data() + size_; }
	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }

	// ordered lookup (std::flat_set surface)

	/// First element not ordered before `key` by (length, address).
	[[nodiscard]] iterator lower_bound(const EmptySpaceRecord& key) noexcept {
		return std::ranges::lower_bound(begin(), end(), key);
	}
	/// First element whose `length` is >= `length` (projection on the length word only).
	[[nodiscard]] iterator lower_bound_by_length(uint32_t length) noexcept {
		return std::ranges::lower_bound(begin(), end(), length, {}, &EmptySpaceRecord::length);
	}
	/// Iterator to an element equal to `key`, or `end()` if absent.
	[[nodiscard]] iterator find(const EmptySpaceRecord& key) noexcept {
		iterator it = lower_bound(key);
		if (it != end() && *it == key) {
			return it;
		}
		return end();
	}

	// mutation — flat_set style; no throw, no realloc

	/// Sorted insert. Returns `{iterator-to-inserted, true}`, or `{end(), false}` if the buffer is
	/// full (the buffer and `size()` are left untouched in that case). Duplicates are permitted
	/// (multiset semantics), matching the legacy array.
	std::pair<iterator, bool> insert(const EmptySpaceRecord& value) noexcept {
		if (full()) {
			return {end(), false};
		}
		iterator pos = lower_bound(value);
		std::move_backward(pos, end(), end() + 1);
		*pos = value;
		++size_;
		return {pos, true};
	}

	/// Erase the element at `pos`; returns the iterator following the removed element.
	iterator erase(const_iterator pos) noexcept {
		iterator p = begin() + (pos - cbegin());
		std::move(p + 1, end(), p);
		--size_;
		return p;
	}

	/// Erase the first element equal to `key`. Returns 1 if one was removed, 0 if absent (no
	/// mutation on miss).
	size_type erase(const EmptySpaceRecord& key) noexcept {
		iterator it = find(key);
		if (it == end()) {
			return 0;
		}
		erase(it);
		return 1;
	}

private:
	std::span<EmptySpaceRecord> storage_{}; // capacity view over the external static buffer
	size_type size_ = 0;
};
