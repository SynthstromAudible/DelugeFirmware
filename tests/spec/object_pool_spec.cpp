#include "../tracking_allocator.h"
#include "memory/object_pool.h"

#include "cppspec.hpp"
#include <cassert>
#include <future>
#include <thread>

using namespace deluge::memory;

using ObjectPoolType = ObjectPool<int, TrackingAllocator>;

// clang-format off
describe object_pool("ObjectPool", $ {
	before_each([] {
		ObjectPoolType::get().resize(16);
		TrackingAllocator<int>::reset();
		ObjectPoolType::get().repopulate();
	});

	after_each([] {
		ObjectPoolType::get().clear();
		assert(TrackingAllocator<int>::num_outstanding() == 0);
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
