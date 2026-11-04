#include "modulation/arp_ratchet.h"

#include "cppspec.hpp"
#include <cstdint>

using deluge::plugin::SyncLevel;

// clang-format off
describe arp_ratchet("an arp ratchet's doublings at a sync level", ${
	it("are as drawn at the coarser levels", _ {
		for (uint32_t drawn = 0; drawn <= 3; ++drawn) {
			expect(ratchet_doublings(SyncLevel::SIXTEENTH, drawn)).to_equal(drawn);
			expect(ratchet_doublings(SyncLevel::THIRTY_SECOND, drawn)).to_equal(drawn);
		}
	});
	it("allow at most four notes at 1/64 and two at 1/128", _ {
		expect(ratchet_doublings(SyncLevel::SIXTY_FOURTH, 3)).to_equal(2U);
		expect(ratchet_doublings(SyncLevel::SIXTY_FOURTH, 1)).to_equal(1U);
		expect(ratchet_doublings(SyncLevel::ONE_HUNDRED_TWENTY_EIGHTH, 3)).to_equal(1U);
	});
	it("stay none when no ratchet is drawn, at every level", _ {
		expect(ratchet_doublings(SyncLevel::SIXTY_FOURTH, 0)).to_equal(0U);
		expect(ratchet_doublings(SyncLevel::ONE_HUNDRED_TWENTY_EIGHTH, 0)).to_equal(0U);
	});
});
// clang-format on

CPPSPEC_SPEC(arp_ratchet)
