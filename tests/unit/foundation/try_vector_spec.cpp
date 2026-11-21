// TryVector: a contiguous vector whose growth reports AllocError instead of throwing, leaving the vector as it was.

#include "foundation/try_vector.h"
#include "support/mocks/failing_allocator.h"

#include "cppspec.hpp"

#include <memory>
#include <new>
#include <string>
#include <vector>

using test::FailingAllocator;
using Ints = deluge::TryVector<int, FailingAllocator<int>>;
using Ptrs = deluge::TryVector<std::unique_ptr<int>, FailingAllocator<std::unique_ptr<int>>>;

static_assert(!std::is_copy_constructible_v<Ints>);
static_assert(std::is_nothrow_move_constructible_v<Ints>);

namespace {
std::vector<int> contents(const Ints& v) {
	return {v.begin(), v.end()};
}
} // namespace

// clang-format off
describe try_vector("deluge::TryVector", $ {
	before_each([] { FailingAllocator<int>::never_fail(); });
	after_each([] { FailingAllocator<int>::never_fail(); });

	it("grows like libstdc++: doubling from the size, exact on reserve", _ {
		Ints v;
		expect(v.try_push_back(1)).to_have_value();
		expect(v.capacity()).to_equal(1u);
		(void)v.try_push_back(2);
		expect(v.capacity()).to_equal(2u);
		(void)v.try_push_back(3);
		expect(v.capacity()).to_equal(4u);
		expect(v.try_reserve(9)).to_have_value();
		expect(v.capacity()).to_equal(9u);
		expect(contents(v)).to_equal(std::vector<int>{1, 2, 3});
	});

	it("leaves the contents unchanged when growth fails", _ {
		Ints v;
		(void)v.try_push_back(1);
		(void)v.try_push_back(2);
		FailingAllocator<int>::fail_after(0);
		auto r = v.try_push_back(3);
		expect(r).not_().to_have_value();
		expect(r.error()).to_equal(deluge::AllocError::OUT_OF_MEMORY);
		expect(contents(v)).to_equal(std::vector<int>{1, 2});
		expect(v.capacity()).to_equal(2u);
	});

	it("does not allocate while within capacity", _ {
		Ints v;
		(void)v.try_reserve(4);
		FailingAllocator<int>::fail_after(0);
		expect(v.try_push_back(1)).to_have_value();
		expect(v.try_insert(v.begin(), 0)).to_have_value();
		expect(v.try_insert(v.end(), 2u, 9)).to_have_value();
		expect(contents(v)).to_equal(std::vector<int>{0, 1, 9, 9});
		expect(v.try_push_back(5)).not_().to_have_value();
	});

	it("inserts in the middle, with and without reallocating", _ {
		const std::vector<int> start{1, 4};
		Ints v;
		v.assign(start.begin(), start.end());
		auto r = v.try_insert(v.begin() + 1, 2u, 7);
		expect(r).to_have_value();
		expect(*r - v.begin()).to_equal(1);
		expect(contents(v)).to_equal(std::vector<int>{1, 7, 7, 4});
		(void)v.try_reserve(8);
		(void)v.try_emplace(v.begin() + 2, 5);
		expect(contents(v)).to_equal(std::vector<int>{1, 7, 5, 7, 4});
		(void)v.try_insert(v.begin() + 1, 3u, 2); // within capacity, the gap straddling the old end
		expect(contents(v)).to_equal(std::vector<int>{1, 2, 2, 2, 7, 5, 7, 4});
	});

	it("inserts a copy of one of its own elements safely", _ {
		Ints v;
		(void)v.try_push_back(3);
		(void)v.try_push_back(8); // capacity 2: the next insert reallocates
		(void)v.try_insert(v.begin(), v[1]);
		expect(contents(v)).to_equal(std::vector<int>{8, 3, 8});
		(void)v.try_reserve(8);
		(void)v.try_insert(v.begin(), v[2]); // and within capacity
		expect(contents(v)).to_equal(std::vector<int>{8, 8, 3, 8});
	});

	it("resizes up with value-initialised elements and down by destroying the tail", _ {
		Ints v;
		expect(v.try_resize(3)).to_have_value();
		expect(contents(v)).to_equal(std::vector<int>{0, 0, 0});
		(void)v.try_resize(1);
		expect(v.size()).to_equal(1u);
	});

	it("erases a range and keeps order", _ {
		Ints v;
		for (int i = 0; i < 5; ++i) {
			v.push_back(i);
		}
		auto after = v.erase(v.begin() + 1, v.begin() + 3);
		expect(*after).to_equal(3);
		expect(contents(v)).to_equal(std::vector<int>{0, 3, 4});
	});

	it("erases an empty range without touching the elements", _ {
		using Strings = deluge::TryVector<std::string, FailingAllocator<std::string>>;
		Strings v;
		v.push_back("kept");
		v.erase(v.begin(), v.begin()); // a self-move of the tail would empty the string
		expect(v.size()).to_equal(1u);
		expect(v[0]).to_equal(std::string{"kept"});
	});

	it("clones fallibly, leaving no allocation behind on failure", _ {
		Ints v;
		v.push_back(1);
		v.push_back(2);
		auto c = v.try_clone();
		expect(c.has_value()).to_be_true(); // expect() copies its value, and a TryVector cannot be copied
		expect(contents(*c)).to_equal(contents(v));
		const auto before = FailingAllocator<int>::live();
		FailingAllocator<int>::fail_after(0);
		expect(v.try_clone().has_value()).to_be_false();
		expect(FailingAllocator<int>::live()).to_equal(before);
	});

	it("holds move-only elements and moves them on reallocation", _ {
		Ptrs v;
		(void)v.try_emplace_back(std::make_unique<int>(1));
		(void)v.try_emplace_back(std::make_unique<int>(2));
		(void)v.try_emplace_back(std::make_unique<int>(3));
		expect(*v[0] + *v[1] + *v[2]).to_equal(6);
	});

	it("frees everything it allocated", _ {
		const auto before = FailingAllocator<int>::live();
		{
			Ints v;
			for (int i = 0; i < 100; ++i) {
				v.push_back(i);
			}
			Ints w = std::move(v);
			expect(v.empty()).to_be_true();
			expect(w.size()).to_equal(100u);
		}
		expect(FailingAllocator<int>::live()).to_equal(before);
	});

	it("calls oom() from the infallible members", _ {
		Ints v;
		FailingAllocator<int>::fail_after(0);
		bool threw = false;
		try {
			v.push_back(1);
		} catch (const std::bad_alloc&) {
			threw = true;
		}
		expect(threw).to_be_true();
	});
});

CPPSPEC_SPEC(try_vector)
