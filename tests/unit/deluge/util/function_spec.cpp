#include "util/const_functions.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe function_group("function", ${
	it("mod", _ {
		expect(mod(-3, 3)).to_equal(0);
		expect(mod(-2, 3)).to_equal(1);
		expect(mod(-1, 3)).to_equal(2);
		expect(mod(0, 3)).to_equal(0);
		expect(mod(1, 3)).to_equal(1);
		expect(mod(2, 3)).to_equal(2);
		expect(mod(3, 3)).to_equal(0);
	});
});

DELUGE_SPEC(function_spec, function_group)
