// base/error.h: the codes keep the numbers they cross the libdeluge ABI with, and the type keeps its size.
#include "base/error.h"

#include <cstdint>
#include <utility>

#include "support/spec_main.h"

using deluge::Error;

// clang-format off
describe error_spec_body("base/error.h", ${
	it("keeps the numbers the ABI and the Rust loader use", _ {
		expect(std::to_underlying(Error::NONE)).to_equal(0);
		expect(std::to_underlying(Error::INSUFFICIENT_RAM)).to_equal(1);
		expect(std::to_underlying(Error::FILE_NOT_FOUND)).to_equal(18);
		expect(std::to_underlying(Error::ABORTED_BY_USER)).to_equal(19);
		expect(std::to_underlying(Error::TOO_MANY_OPEN_STREAMS)).to_equal(34);
		expect(std::to_underlying(Error::LISTING_DEFERRED)).to_equal(36);
	});

	it("counts one past the last code, in a 32-bit type", _ {
		expect(deluge::kErrorCount).to_equal(std::size_t{37});
		expect(sizeof(Error)).to_equal(sizeof(int32_t));
	});
});
// clang-format on

DELUGE_SPEC(error_spec, error_spec_body)
