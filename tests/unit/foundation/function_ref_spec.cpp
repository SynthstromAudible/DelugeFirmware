// deluge::function_ref calls the callable it refers to, through a capture by reference, and returns its value.

#include "foundation/function_ref.h"

#include "cppspec.hpp"

namespace {

int twice(deluge::function_ref<int(int)> f, int x) {
	return f(f(x));
}

} // namespace

// clang-format off
describe function_ref("deluge::function_ref", $ {
	it("returns the callable's value", _ {
		expect(twice([](int x) { return x + 3; }, 1)).to_equal(7);
	});
	it("calls through to state the lambda captures by reference", _ {
		int calls = 0;
		auto counting = [&](int x) {
			++calls;
			return x;
		};
		expect(twice(counting, 5)).to_equal(5);
		expect(calls).to_equal(2);
	});
	it("refers to the callable, not a copy of it", _ {
		struct Counter {
			int n = 0;
			int operator()(int x) { return n += x; }
		} counter;
		deluge::function_ref<int(int)> ref{counter};
		ref(2);
		ref(3);
		expect(counter.n).to_equal(5);
	});
});
// clang-format on

CPPSPEC_SPEC(function_ref)
