#include "util/segmented_vector.h"

#include "cppspec.hpp"
#include <cstddef>
#include <new>

using deluge::SegmentedVector;

namespace {
/// Lifetime-counting, move-only element: proves the container constructs on
/// grow, destroys on shrink, and never copies.
struct Tracked {
	int value = -1;
	static inline int live = 0;
	Tracked() { ++live; }
	Tracked(const Tracked&) = delete;
	Tracked& operator=(const Tracked&) = delete;
	~Tracked() { --live; }
};

/// @brief Minimal, self-contained non-default `Alloc` for `SegmentedVector`'s
///        `template <typename> class Alloc` parameter. Deliberately NOT
///        `fast_allocator` (that needs a live BSP heap, which this host CppSpec
///        harness doesn't have) -- this exists purely to prove the allocator
///        template plumbing (both `SegAlloc` for segment storage and the
///        segment-pointer index's `std::vector<Segment*, Alloc<Segment*>>`)
///        actually threads a non-default allocator through end-to-end.
template <typename U>
struct MinimalAlloc {
	using value_type = U;

	MinimalAlloc() = default;
	template <typename V>
	MinimalAlloc(const MinimalAlloc<V>&) noexcept {} // NOLINT: allocator rebind ctor

	[[nodiscard]] U* allocate(std::size_t n) { return static_cast<U*>(::operator new(n * sizeof(U))); }
	void deallocate(U* p, std::size_t /*n*/) noexcept { ::operator delete(p); }

	template <typename V>
	bool operator==(const MinimalAlloc<V>&) const noexcept {
		return true;
	}
	template <typename V>
	bool operator!=(const MinimalAlloc<V>&) const noexcept {
		return false;
	}
};
} // namespace

// clang-format off
describe segmented_vector("SegmentedVector<T, N>", $ {
	it("default-constructs empty", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		expect(v.size()).to_equal(std::size_t{0});
		expect(v.empty()).to_be_true();
	});

	it("grows, default-constructing each new element", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.resize(3);
		expect(v.size()).to_equal(std::size_t{3});
		expect(Tracked::live).to_equal(3);
		expect(v[0].value).to_equal(-1);
	});

	it("round-trips values written through operator[]", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.resize(10);
		for (std::size_t i = 0; i < 10; ++i) { v[i].value = static_cast<int>(i * 7); }
		for (std::size_t i = 0; i < 10; ++i) {
			expect(v[i].value).to_equal(static_cast<int>(i * 7));
		}
	});

	it("keeps existing element addresses stable across growth (the load-bearing property)", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.resize(2);
		v[1].value = 99;
		Tracked* addr = &v[1];
		v.resize(1000); // many new segments
		expect(&v[1] == addr).to_be_true();
		expect(v[1].value).to_equal(99);
	});

	it("indexes correctly across segment boundaries", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.resize(13); // spans 4 segments of 4
		for (std::size_t i = 0; i < 13; ++i) { v[i].value = static_cast<int>(100 + i); }
		expect(v[3].value).to_equal(103);   // end of segment 0
		expect(v[4].value).to_equal(104);   // start of segment 1
		expect(v[12].value).to_equal(112);  // segment 3
	});

	it("destroys the removed tail on shrink", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.resize(10);
		expect(Tracked::live).to_equal(10);
		v.resize(3);
		expect(v.size()).to_equal(std::size_t{3});
		expect(Tracked::live).to_equal(3);
	});

	it("destroys all elements when destructed (no leak)", _{
		Tracked::live = 0;
		{
			SegmentedVector<Tracked, 4> v;
			v.resize(20);
			expect(Tracked::live).to_equal(20);
		}
		expect(Tracked::live).to_equal(0);
	});

	it("keeps addresses stable and values intact across growth after reserve(n)", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.reserve(1000);           // pre-reserve the segment-pointer index to the final capacity
		v.resize(2);
		v[0].value = 7;
		v[1].value = 99;
		Tracked* addr0 = &v[0];
		Tracked* addr1 = &v[1];
		v.resize(1000);            // grow across many segments; index must not reallocate/move elements
		expect(&v[0] == addr0).to_be_true();
		expect(&v[1] == addr1).to_be_true();
		expect(v[0].value).to_equal(7);
		expect(v[1].value).to_equal(99);
		expect(v.size()).to_equal(std::size_t{1000});
		expect(Tracked::live).to_equal(1000);
	});

	it("drains fully on a direct resize(0)", _{
		Tracked::live = 0;
		SegmentedVector<Tracked, 4> v;
		v.resize(10);
		expect(Tracked::live).to_equal(10);
		v.resize(0);
		expect(v.size()).to_equal(std::size_t{0});
		expect(v.empty()).to_be_true();
		expect(Tracked::live).to_equal(0);
	});

	it("plumbs a non-default Alloc through both segment storage and the pointer index", _{
		Tracked::live = 0;
		{
			SegmentedVector<Tracked, 4, MinimalAlloc> v;
			v.resize(10); // grow: spans 3 segments of 4, allocated via MinimalAlloc
			expect(v.size()).to_equal(std::size_t{10});
			expect(Tracked::live).to_equal(10);

			v[7].value = 42;
			expect(v[7].value).to_equal(42);

			v.resize(3); // shrink: destroys tail, frees trailing segments via MinimalAlloc
			expect(v.size()).to_equal(std::size_t{3});
			expect(Tracked::live).to_equal(3);
		}
		expect(Tracked::live).to_equal(0); // destructor freed the rest via MinimalAlloc too
	});
});
// clang-format on

CPPSPEC_SPEC(segmented_vector)
