#include "plugin-api/types/voice_priority.h"

#include "cppspec.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

using deluge::plugin::VoicePriority;

// clang-format off
describe voice_priority("VoicePriority", $ {
	it("is one byte, as songs and records store it", _ {
		expect(std::is_same_v<std::underlying_type_t<VoicePriority>, uint8_t>).to_be_true();
	});

	it("keeps the values the priority attribute saves", _ {
		expect(std::to_underlying(VoicePriority::LOW)).to_equal(0);
		expect(std::to_underlying(VoicePriority::MEDIUM)).to_equal(1);
		expect(std::to_underlying(VoicePriority::HIGH)).to_equal(2);
	});
});

CPPSPEC_SPEC(voice_priority)
