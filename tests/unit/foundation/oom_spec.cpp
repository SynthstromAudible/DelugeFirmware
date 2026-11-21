// or_oom passes a value through and calls deluge::oom() on an error; the spec build's oom() throws std::bad_alloc.

#include "foundation/oom.h"

#include "cppspec.hpp"

#include <new>

// clang-format off
describe oom("deluge::or_oom", $ {
	it("returns the value of a success", _ {
		deluge::AllocResult<int> r{7};
		expect(deluge::or_oom(std::move(r))).to_equal(7);
	});
	it("calls oom() on an AllocError", _ {
		bool threw = false;
		try {
			(void)deluge::or_oom(deluge::AllocResult<int>{std::unexpect, deluge::AllocError::OUT_OF_MEMORY});
		} catch (const std::bad_alloc&) {
			threw = true;
		}
		expect(threw).to_be_true();
	});
	it("checks a void result", _ {
		bool threw = false;
		try {
			deluge::or_oom(deluge::AllocResult<void>{std::unexpect, deluge::AllocError::OUT_OF_MEMORY});
		} catch (const std::bad_alloc&) {
			threw = true;
		}
		expect(threw).to_be_true();
	});
});

CPPSPEC_SPEC(oom)
