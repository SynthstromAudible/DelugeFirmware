#include "../tracking_allocator.h"

#include "cppspec.hpp"

using TrackingAllocatorType = TrackingAllocator<int>;

// clang-format off
describe tracking_allocator("TrackingAllocator", $ {
	before_each([] {
		TrackingAllocatorType::reset();
	});

	it("should track allocations", _{
		TrackingAllocatorType ta;
		auto *p = ta.allocate(1);
		expect(ta.num_allocated()).to_equal(1);
		ta.deallocate(p, 1);
	});

	it("should track deallocations", _{
		TrackingAllocatorType ta;
		auto *p = ta.allocate(1);
		ta.deallocate(p, 1);
		expect(ta.num_deallocated()).to_equal(1);
	});

	it("should track outstanding allocations", _{
		TrackingAllocatorType ta;
		auto *p = ta.allocate(1);
		expect(ta.num_outstanding()).to_equal(1);
		ta.deallocate(p, 1);
		expect(ta.num_outstanding()).to_equal(0);
	});

	it("should track whether a pointer is allocated", _{
		TrackingAllocatorType ta;
		auto* p = ta.allocate(1);
		expect(ta.is_allocated(p)).to_be_true();
		ta.deallocate(p, 1);
		expect(ta.is_allocated(p)).to_be_false();
	});

	it("should track whether a pointer is deallocated", _{
		TrackingAllocatorType ta;
		auto* p = ta.allocate(1);
		ta.deallocate(p, 1);
		expect(ta.is_deallocated(p)).to_be_true();
	});

	it("should track leaks", _{
		TrackingAllocatorType ta;
		auto* p = ta.allocate(1);
		auto leaked = ta.outstanding();
		expect(leaked.size()).to_equal(1);
		expect(leaked).to_contain(p);
		ta.deallocate(p, 1);
		leaked = ta.outstanding();
		expect(leaked.size()).to_equal(0);
	});
});

CPPSPEC_MAIN(tracking_allocator)
