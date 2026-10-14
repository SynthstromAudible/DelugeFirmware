#pragma once
#include <cstddef>

namespace deluge::harness {

/// @brief A fixed-capacity, no-heap vector.
///
/// A standalone copy of src/shell/fixed_vector.h's shape: tests/harness is compiled into the
/// host BSP and must not include anything from src/shell (see tests/harness/README.md), so it
/// carries its own copy rather than reusing the shell's.
///
/// Push past capacity `N` fails soft (returns false) rather than growing, asserting or
/// throwing.
/// @tparam T Element type, default-constructible (`items_[N]{}`).
/// @tparam N Capacity, fixed at compile time.
template <class T, size_t N>
class FixedVector {
public:
	/// @brief Append \p v if there is room.
	/// @param v Value to copy in.
	/// @return False if the vector was already at capacity `N` (nothing is appended).
	bool push_back(const T& v) {
		if (n_ == N) {
			return false;
		}
		items_[n_++] = v;
		return true;
	}
	/// @brief Element access, no bounds check (mirrors `std::array::operator[]`).
	/// @param i Index, caller-guaranteed `< size()`.
	T& operator[](size_t i) { return items_[i]; }
	/// @copydoc operator[]
	const T& operator[](size_t i) const { return items_[i]; }
	/// @brief Number of elements currently held.
	size_t size() const { return n_; }
	/// @brief True if `size() == 0`.
	bool empty() const { return n_ == 0; }
	/// @brief Discard every element (`size()` becomes 0); capacity is unchanged.
	void clear() { n_ = 0; }

	/// @name Iteration
	/// @{
	T* begin() { return items_; }
	T* end() { return items_ + n_; }
	const T* begin() const { return items_; }
	const T* end() const { return items_ + n_; }
	/// @}

private:
	T items_[N]{};
	size_t n_ = 0;
};

} // namespace deluge::harness
