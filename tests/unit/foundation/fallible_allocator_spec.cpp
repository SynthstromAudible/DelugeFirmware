// nothrow_new_allocator returns aligned memory, or null rather than throwing; the concept tells fallible allocators
// from the standard one.

#include "foundation/fallible_allocator.h"
#include "support/mocks/failing_allocator.h"

#include "cppspec.hpp"

#include <cstdint>
#include <memory>

static_assert(deluge::FallibleAllocator<test::FailingAllocator<int>, int>);
static_assert(deluge::FallibleAllocator<deluge::nothrow_new_allocator<double>, double>);
static_assert(!deluge::FallibleAllocator<std::allocator<int>, int>);

namespace {
struct alignas(64) Wide {
	std::byte b[64];
};
} // namespace

// clang-format off
describe fallible_allocator("deluge::nothrow_new_allocator", $ {
	it("returns aligned memory it can free", _ {
		deluge::nothrow_new_allocator<Wide> a;
		Wide* p = a.try_allocate(3);
		expect(p).not_().to_equal(static_cast<Wide*>(nullptr));
		expect(reinterpret_cast<std::uintptr_t>(p) % 64).to_equal(std::uintptr_t{0});
		a.deallocate(p, 3);
	});
	it("returns null for a size that overflows", _ {
		deluge::nothrow_new_allocator<std::uint64_t> a;
		expect(a.try_allocate(SIZE_MAX / 4)).to_equal(static_cast<std::uint64_t*>(nullptr));
	});
});

CPPSPEC_SPEC(fallible_allocator)
