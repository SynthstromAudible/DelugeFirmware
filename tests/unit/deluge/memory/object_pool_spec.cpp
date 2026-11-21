#include "memory/object_pool.h"
#include "support/mocks/failing_allocator.h"
#include "support/mocks/tracking_allocator.h"

#include <cassert>
#include <future>
#include <thread>
#include <vector>

#include "cppspec.hpp"

using namespace deluge::memory;

using ObjectPoolType = ObjectPool<int, TrackingAllocator>;
using FailingPool = ObjectPool<int, test::FailingAllocator>;

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

	// One pool per type, shared by every context, is the contract `recycle` already implements —
	// a per-context (e.g. `thread_local`) pool cannot coexist with it:
	//
	//   - `recycle` is static and calls `get()`, so it returns the object to the pool of whichever
	//     context DESTROYS it, not the one that acquired it. Per-context pools therefore let
	//     objects migrate, which makes each pool's capacity accounting wrong.
	//   - Worse, once a pool is at capacity `recycle` falls through to `Alloc<T>().deallocate`, so
	//     a migrated object turns the pool into a plain heap allocator — defeating the whole point
	//     of pooling.
	//   - The global object ceiling must stay capacity, not capacity x contexts: callers (e.g. the
	//     voices_ race analysis) rely on that single global bound.
	//   - On the firmware a per-context pool is unsafe at a lower level too: nothing initialises
	//     the ARM TPIDRURO thread pointer, so `thread_local` resolves through an uninitialised
	//     register and hands back a wild pointer — a boot-time abort in AudioEngine::init().
	it("shares one pool across threads", _{
		auto& op = ObjectPoolType::get();
		std::promise<void*> promise;
		std::future<void*> future = promise.get_future();

		std::thread thread([&promise]() {
			void* op = &ObjectPoolType::get();
			promise.set_value(op);
		});
		thread.join();
		expect(&op).to_equal(future.get());
	});

	it("try_acquire falls back to the heap when the pool is empty", _{
		auto& op = ObjectPoolType::get();
		std::vector<ObjectPoolType::pointer_type> held;
		for (size_t i = 0; i <= op.capacity(); ++i) {
			auto obj = op.try_acquire(static_cast<int>(i));
			expect(obj.has_value()).to_be_true();
			held.push_back(std::move(*obj));
		}
		expect(*held.back()).to_equal(static_cast<int>(op.capacity()));
	});

	it("try_acquire returns nullopt when the pool is empty and the heap has nothing", _{
		auto& op = FailingPool::get();
		op.clear();
		test::FailingAllocator<int>::fail_after(0);
		expect(op.try_acquire(1).has_value()).to_be_false();
		test::FailingAllocator<int>::never_fail();
	});

	it("repopulate stops at the first failure and reports how many it pooled", _{
		auto& op = FailingPool::get();
		op.clear();
		op.resize(8);
		// The free list comes from FailingAllocator<int*>, which keeps its own count and never fails here.
		test::FailingAllocator<int>::fail_after(3);
		expect(op.repopulate()).to_equal(std::size_t{3});
		test::FailingAllocator<int>::never_fail();
		op.clear();
	});

	it("frees a recycled object rather than grow a free list with no room reserved", _{
		// A pool whose first repopulate() (in its constructor) could not reserve its free list, as at a boot with no
		// memory: room for 48 objects by count, none by reservation.
		using StarvedPool = ObjectPool<short, test::FailingAllocator>;
		test::FailingAllocator<short*>::fail_after(0);
		auto& op = StarvedPool::get();
		test::FailingAllocator<short*>::never_fail();
		expect(op.size()).to_equal(std::size_t{0});
		const auto before = test::FailingAllocator<short>::live();
		{
			auto obj = op.try_acquire(short{7}); // from the heap
			expect(obj.has_value()).to_be_true();
		} // recycled: the list has no room, so the slot is freed instead of pushed
		expect(test::FailingAllocator<short>::live()).to_equal(before);
		expect(op.size()).to_equal(std::size_t{0});
	});
});

CPPSPEC_SPEC(object_pool);
