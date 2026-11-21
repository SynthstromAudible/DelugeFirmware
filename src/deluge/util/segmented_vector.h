#pragma once

#include "foundation/fallible_allocator.h"
#include "foundation/oom.h"
#include "foundation/try_vector.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace deluge {

/// @brief Growable, indexable container whose elements NEVER move once
///        constructed: `T` lives in fixed-size heap segments and growth appends
///        segments, so a `T&`/`T*` from `operator[]` stays valid across all later
///        growth.
///
/// This is the property `std::vector` cannot give (its backing array reallocates
/// and moves on growth). It exists for state shared across threads where one side
/// may grow the container while another holds a reference into it — see
/// `SampleRecorder::bufferTable_` and docs/dev/known-concurrency-bugs.md (B2).
///
/// @note Single-writer for structural mutation (`try_resize` / `resize`). Concurrent readers may
///       hold references from `operator[]` across a grow. Shrinking destroys the
///       removed tail — same contract as `std::vector::resize` to a smaller size:
///       it must not race a reader of those tail elements.
///
/// @tparam T           Element type; default-constructible, nothrow-destructible;
///                     may be move-only.
/// @tparam SegmentSize Elements per segment; a compile-time power of two.
/// @tparam Alloc       Segment/heap allocator, a FallibleAllocator; defaults to
///                     `deluge::nothrow_new_allocator` so this header stays BSP-free
///                     (links in the plain host CppSpec harness with no initialized
///                     heap); firmware call sites pass a heap-specific allocator
///                     (e.g. `deluge::memory::fast_allocator`) to control placement.
template <typename T, std::size_t SegmentSize = 256, template <typename> class Alloc = deluge::nothrow_new_allocator>
class SegmentedVector {
	static_assert((SegmentSize & (SegmentSize - 1)) == 0 && SegmentSize != 0,
	              "SegmentSize must be a non-zero power of two");

	struct Segment {
		// User-provided (not defaulted) default ctor: makes Segment a non-trivially-default-constructible
		// type so `std::construct_at(a.allocate(1))` value-initialization runs THIS no-op ctor instead of
		// zero-filling `storage`. The storage bytes are raw backing for `T` slots that are individually
		// lifetime-managed by resize/pop_back; zero-filling them on every segment alloc is pure wasted
		// SRAM write traffic.
		Segment() noexcept {}
		alignas(T) std::byte storage[sizeof(T) * SegmentSize];
	};

	static constexpr std::size_t kMask = SegmentSize - 1;
	static constexpr std::size_t kShift = std::countr_zero(SegmentSize);
	using SegAlloc = Alloc<Segment>;

public:
	/// @brief The segment type, so a spec can make its allocator fail.
	using segment_type = Segment;

	SegmentedVector() = default;
	SegmentedVector(const SegmentedVector&) = delete;
	SegmentedVector& operator=(const SegmentedVector&) = delete;

	SegmentedVector(SegmentedVector&& other) noexcept
	    : segments_{std::move(other.segments_)}, size_{std::exchange(other.size_, 0)} {}
	SegmentedVector& operator=(SegmentedVector&& other) noexcept {
		if (this != &other) {
			clear_and_free();
			segments_ = std::move(other.segments_);
			size_ = std::exchange(other.size_, 0);
		}
		return *this;
	}

	~SegmentedVector() { clear_and_free(); }

	/// @return The number of live elements.
	[[nodiscard]] std::size_t size() const { return size_; }
	/// @return `true` if there are no live elements.
	[[nodiscard]] bool empty() const { return size_ == 0; }

	/// @param i Element index; must be `< size()`.
	/// @return A reference to element @p i. Stable across all later growth (see class docs).
	[[nodiscard]] T& operator[](std::size_t i) { return *slot(i); }
	/// @copydoc operator[](std::size_t)
	[[nodiscard]] const T& operator[](std::size_t i) const { return *slot(i); }

	/// @brief Resize to exactly @p n: grow default-constructs new tail elements,
	///        shrink destroys removed tail elements. Existing elements never move.
	///
	/// @warning Growth reallocates the segment index unless the caller pre-reserved (see `try_reserve`). Growth
	///          CONCURRENT with a reader (the recorder's audio thread growing while the card routine reads via
	///          `operator[]`) MUST reserve to the final capacity first, single-threaded.
	/// @return AllocError when a segment or the index cannot be had; nothing has changed then (no segment or element
	///         is left behind).
	[[nodiscard]] AllocResult<void> try_resize(std::size_t n) {
		if (n <= size_) {
			while (size_ > n) {
				pop_back();
			}
			return {};
		}
		const std::size_t segmentsBefore = segments_.size();
		const std::size_t segmentsNeeded = (n + SegmentSize - 1) >> kShift;
		if (segmentsNeeded > segments_.capacity()) {
			// Geometric, as push_back grew the index before: back-to-back growth reallocates it rarely.
			if (auto r = segments_.try_reserve(std::max(segmentsNeeded, 2 * segments_.size())); !r) {
				return r;
			}
		}
		while (segments_.size() < segmentsNeeded) {
			Segment* seg = try_alloc_segment();
			if (seg == nullptr) {
				while (segments_.size() > segmentsBefore) {
					free_segment(segments_.back());
					segments_.pop_back();
				}
				return std::unexpected(AllocError::OUT_OF_MEMORY);
			}
			segments_.push_back(seg); // within the capacity reserved above: cannot allocate
		}
		// Construct through the RAW address, not `slot(size_)`: no `T` exists at this index yet, so laundering here
		// would be UB (nothing to launder). `slot` (which launders) is used only where a live object exists
		// (`operator[]`, `pop_back`).
		for (; size_ < n; ++size_) {
			std::construct_at(reinterpret_cast<T*>(raw_at(size_)));
		}
		return {};
	}

	/// @brief try_resize, or oom().
	void resize(std::size_t n) { or_oom(try_resize(n)); }

	/// @brief Reserve the internal segment-pointer index so growth up to @p n elements will not
	///        reallocate it. Does NOT allocate any Segment and does NOT construct any element.
	///
	/// After `try_reserve(N)`, growth up to N elements will not reallocate the internal segment-pointer
	/// array. Growing this container CONCURRENTLY with readers (e.g. the recorder's audio-thread growth
	/// via `resize` vs the card routine's `operator[]`/`chunk_at`) REQUIRES reserving to the final capacity
	/// first, single-threaded; single-threaded growth is unrestricted.
	/// @return AllocError when the index cannot be had (it is then as it was).
	[[nodiscard]] AllocResult<void> try_reserve(std::size_t n) {
		return segments_.try_reserve((n + SegmentSize - 1) / SegmentSize);
	}

	/// @brief try_reserve, or oom().
	void reserve(std::size_t n) { or_oom(try_reserve(n)); }

private:
	/// @return The RAW backing address of index @p i. No object need exist there — use this for
	///         `construct_at` (starting a lifetime) where laundering would be UB.
	[[nodiscard]] std::byte* raw_at(std::size_t i) const {
		return segments_[i >> kShift]->storage + (i & kMask) * sizeof(T);
	}
	/// @return A launderable `T*` for index @p i. Use ONLY where a live `T` exists (access/destroy).
	[[nodiscard]] T* slot(std::size_t i) const { return std::launder(reinterpret_cast<T*>(raw_at(i))); }

	/// @return A new Segment, or nullptr when the allocator has none.
	static Segment* try_alloc_segment() {
		SegAlloc a;
		Segment* raw = a.try_allocate(1);
		if (raw == nullptr) {
			return nullptr;
		}
		// `try_allocate(1)` only returns raw storage; it does not start Segment's
		// lifetime. Implicit-lifetime-object creation (P0593) is guaranteed for
		// `std::allocator` (built on `::operator new`), but NOT for an arbitrary
		// `Alloc` -- `deluge::memory::fast_allocator` routes through the bespoke
		// `deluge::memory::alloc_fast`, which is not a standard
		// implicit-object-creation function. `construct_at` here runs Segment's no-op
		// default ctor (which leaves `storage` uninitialized) and makes this correct for
		// every `Alloc`.
		return std::construct_at(raw);
	}
	static void free_segment(Segment* s) {
		SegAlloc a;
		a.deallocate(s, 1);
	}

	void pop_back() {
		--size_;
		std::destroy_at(slot(size_));
		if ((size_ & kMask) == 0) { // just emptied the trailing segment
			free_segment(segments_.back());
			segments_.pop_back();
		}
	}

	void clear_and_free() {
		while (size_ > 0) {
			pop_back();
		}
		segments_.clear();
	}

	TryVector<Segment*, Alloc<Segment*>> segments_{};
	std::size_t size_ = 0;
};

} // namespace deluge
