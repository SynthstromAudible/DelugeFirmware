#include "effects/delay.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>

using namespace deluge::effects;
namespace plugin = deluge::plugin;

namespace {

/// One synced rate: the rate, its sync, the tick inverse, and the rate the delay runs at.
struct Case {
	int32_t rate;
	plugin::SyncType type;
	plugin::SyncLevel level;
	uint32_t inverse;
	int32_t want;
};

// Each want follows the arithmetic by hand: the rounded high word of rate × inverse (the inverse signed), times 3/2 or
// 2/3 truncating toward zero, clamped to INT32_MAX >> (level + 5), shifted up by level + 5 in 32 bits.
// clang-format off
constexpr std::array<Case, 20> kCases{{
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::WHOLE, 0x00012345U, 18624}, // each level, even
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::HALF, 0x00012345U, 37248},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::QUARTER, 0x00012345U, 74496},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::EIGHTH, 0x00012345U, 148992},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::SIXTEENTH, 0x00012345U, 297984},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::THIRTY_SECOND, 0x00012345U, 595968},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::SIXTY_FOURTH, 0x00012345U, 1191936},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::ONE_HUNDRED_TWENTY_EIGHTH, 0x00012345U, 2383872},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::TWO_HUNDRED_FIFTY_SIXTH, 0x00012345U, 4767744},
    {25165824, plugin::SyncType::EVEN, plugin::SyncLevel::SIXTEENTH, 0x00012345U, 447488}, // each sync type
    {25165824, plugin::SyncType::TRIPLET, plugin::SyncLevel::SIXTEENTH, 0x00012345U, 670720},
    {25165824, plugin::SyncType::DOTTED, plugin::SyncLevel::SIXTEENTH, 0x00012345U, 297984},
    {-20995314, plugin::SyncType::EVEN, plugin::SyncLevel::SIXTEENTH, 0x00012345U, -373760}, // negative and odd: 3/2 and 2/3
    {-20995314, plugin::SyncType::TRIPLET, plugin::SyncLevel::SIXTEENTH, 0x00012345U, -560128}, // truncate toward 0
    {-20995314, plugin::SyncType::DOTTED, plugin::SyncLevel::SIXTEENTH, 0x00012345U, -248832},
    {2147483647, plugin::SyncType::EVEN, plugin::SyncLevel::WHOLE, 0x7fffffffU, 2147483584}, // clamped
    {2147483647, plugin::SyncType::TRIPLET, plugin::SyncLevel::TWO_HUNDRED_FIFTY_SIXTH, 0x7fffffffU, 2147467264},
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::SIXTEENTH, 0x80000000U, 0}, // the inverse is -1: -2^23 << 10
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::SIXTEENTH, 0x7fffffffU, 2147482624}, // the inverse at its top
    {16777216, plugin::SyncType::EVEN, plugin::SyncLevel::WHOLE, 0x00000080U, 64}, // half an LSB rounds up to 1
}};

describe delay_sync("synced_delay_rate", ${
	it("passes the rate through unsynced", _ {
		expect(synced_delay_rate(1 << 24, DelayState{.sync_level = plugin::SyncLevel::NONE}, 0x40000000U))
		    .to_equal(1 << 24);
	});
	it("scales the rate by the tick inverse at each level and type, clamped before the level's shift", _ {
		for (const Case& c : kCases) {
			const DelayState settings{.sync_type = c.type, .sync_level = c.level};
			expect(synced_delay_rate(c.rate, settings, c.inverse)).to_equal(c.want);
		}
	});
});
// clang-format on

} // namespace

CPPSPEC_SPEC(delay_sync)
