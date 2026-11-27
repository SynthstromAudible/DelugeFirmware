#include "../tracking_allocator.h"
#include "memory/object_pool.h"

#include <cassert>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "cppspec.hpp"

using namespace deluge::memory;

// Free objects hold the pool's free-list link, so the pooled type must be at least pointer-sized
using ObjectPoolType = ObjectPool<std::uintptr_t, TrackingAllocator>;
using Tracker = TrackingAllocator<std::uintptr_t>;

// clang-format off
describe object_pool("ObjectPool", $ {
	before_each([] {
		ObjectPoolType::get().resize(16);
		Tracker::reset();
		ObjectPoolType::get().repopulate();
	});

	after_each([] {
		ObjectPoolType::get().clear();
		assert(Tracker::num_outstanding() == 0);
	});

	it("should create an object pool of 16 elements by default", _{
		expect(ObjectPoolType::get().capacity()).to_equal(16);
		expect(ObjectPoolType::get().size()).to_equal(16);
	});

	it("should allow resizing the pool", _{
		auto& op = ObjectPoolType::get();
		op.resize(32);
		expect(op.capacity()).to_equal(32);
		expect(op.size()).to_equal(16);
	});

	it("should shrink to a new capacity", _{
		auto& op = ObjectPoolType::get();
		op.resize(8);
		expect(op.capacity()).to_equal(8);
		expect(op.size()).to_equal(8);
	});

	it("should only grow on repopulate", _{
		auto& op = ObjectPoolType::get();
		op.resize(32);
		expect(op.size()).to_equal(16);
		op.repopulate();
		expect(op.capacity()).to_equal(32);
		expect(op.size()).to_equal(32);
	});

	it("acquiring an object decrements the size", _{
		auto& op = ObjectPoolType::get();
		auto obj = op.acquire();
		expect(op.size()).to_equal(15);
	});

	it("automatically recycles objects out of scope", _{
		auto& op = ObjectPoolType::get();
		{
			auto obj = op.acquire();
			expect(op.size()).to_equal(15);
		}
		expect(op.size()).to_equal(16);
	});

	it("offers the last recycled object as the next to be acquired", _{
		auto& op = ObjectPoolType::get();
		void* address = nullptr;
		{
			auto obj = op.acquire();
			address = obj.get();
		}
		auto obj = op.acquire();
		expect(obj.get()).to_equal(address);
	});

	it("should not recycle objects beyond the capacity", _{
		auto& op = ObjectPoolType::get();
		{
			auto obj = op.acquire();
			expect(op.size()).to_equal(15);
			op.repopulate();
			expect(op.size()).to_equal(16);
		}
		expect(op.size()).to_equal(16);
	});

	it("allocates a repopulate as one block", _{
		auto& op = ObjectPoolType::get();
		op.clear();
		Tracker::reset();
		op.repopulate();
		expect(op.size()).to_equal(16);
		expect(Tracker::num_allocated()).to_equal(1);
	});

	it("hands out contiguous objects from the block", _{
		auto& op = ObjectPoolType::get();
		auto a = op.acquire();
		auto b = op.acquire();
		expect(b.get() - a.get()).to_equal(1);
	});

	it("frees overflow objects rather than keeping more than the capacity", _{
		auto& op = ObjectPoolType::get();
		std::vector<ObjectPoolType::pointer_type> held;
		for (int i = 0; i < 20; ++i) {
			held.push_back(op.acquire());
		}
		expect(op.size()).to_equal(0);
		expect(Tracker::num_outstanding()).to_equal(5); // the block plus 4 individual objects
		held.clear();
		expect(op.size()).to_equal(16);
		expect(Tracker::num_outstanding()).to_equal(1); // only the block is kept
	});

	it("keeps the block while one of its objects is in use", _{
		auto& op = ObjectPoolType::get();
		auto obj = op.acquire();
		op.clear();
		expect(Tracker::num_outstanding()).to_equal(1);
		expect(op.size()).to_equal(15); // the block's idle objects stay pooled
		obj.reset();
		expect(op.size()).to_equal(16);
		op.clear();
		expect(Tracker::num_outstanding()).to_equal(0);
	});

	it("empty() should return true when the pool is empty", _{
		auto& op = ObjectPoolType::get();
		op.clear();
		expect(op.empty()).to_be_true();
	});

	it("two threads should have different ObjectPools", _{
		auto& op = ObjectPoolType::get();
		std::promise<void*> promise;
		std::future<void*> future = promise.get_future();

		std::thread thread([&promise]() {
			void* op = &ObjectPoolType::get();
			promise.set_value(op);
		});
		thread.join();
		expect(&op).not_().to_equal(future.get());
	});
});

CPPSPEC_SPEC(object_pool);
