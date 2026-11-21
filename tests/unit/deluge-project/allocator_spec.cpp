// ModelAllocator::try_allocate hands back the host's memory, or null when the host has none, without stopping.

#include "deluge-project/model_memory.h"
#include "foundation/fallible_allocator.h"
#include "model_hooks_for_tests.h"

#include "cppspec.hpp"

using deluge::project::ModelAllocator;
using deluge::project::test::counts;

static_assert(deluge::FallibleAllocator<ModelAllocator<int>, int>);

// clang-format off
describe allocator("ModelAllocator::try_allocate", $ {
	it("returns null when the host has no memory", _ {
		counts.fail_next_allocation = true;
		expect(ModelAllocator<int>{}.try_allocate(4)).to_equal(static_cast<int*>(nullptr));
	});
	it("returns the host's memory otherwise", _ {
		ModelAllocator<int> a;
		int* p = a.try_allocate(4);
		expect(p).not_().to_equal(static_cast<int*>(nullptr));
		a.deallocate(p, 4);
	});
});

CPPSPEC_SPEC(allocator)
