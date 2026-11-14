#include "deluge-project/cv_mode.h"

#include "cppspec.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

using deluge::project::CVMode;

// clang-format off
describe cv_mode("CVMode", $ {
	it("is one byte, as the cv2Source attribute and records store it", _ {
		expect(std::is_same_v<std::underlying_type_t<CVMode>, uint8_t>).to_be_true();
	});

	it("keeps the values cv2Source saves", _ {
		expect(std::to_underlying(CVMode::OFF)).to_equal(0);
		expect(std::to_underlying(CVMode::PITCH)).to_equal(1);
		expect(std::to_underlying(CVMode::MOD)).to_equal(2);
		expect(std::to_underlying(CVMode::AFTERTOUCH)).to_equal(3);
		expect(std::to_underlying(CVMode::VELOCITY)).to_equal(4);
	});
});

CPPSPEC_SPEC(cv_mode)
