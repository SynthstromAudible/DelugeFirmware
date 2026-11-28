#include "support/frames.h"
#include "ui/widgets/oled/keys.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

namespace {

constexpr int32_t kTop = 5; // the 48-row rig's hidden rows: canvas row = local row + 5
constexpr uint16_t kCMajor = 0b1010'1011'0101;
constexpr uint16_t kCMinor = 0b0101'1010'1101;
constexpr Rect kKeys{8, 15, 113, 20}; // white keys 16 wide, black keys 10 wide and 12 tall

} // namespace

// clang-format off
describe octave_w("ui::widgets::oled::octave", $ {
	it("fills the keys in the scale", _ {
		uitest::Rig48 r;
		octave(r.frame, kKeys, kCMajor, 0);
		expect(r.lit(kKeys.x + 2 * 16 + 8, kKeys.y + kKeys.h - 5 + kTop)).to_be_true();  // E, low on the key
		expect(r.lit(kKeys.x + 4 * 16, kKeys.y + 6 + kTop)).to_be_false();              // F#: outlined
		expect(r.lit(kKeys.x + 4 * 16 - 5, kKeys.y + 6 + kTop)).to_be_true();            // F#'s outline
	});

	it("keeps neighbouring filled white keys apart", _ {
		uitest::Rig48 r;
		octave(r.frame, kKeys, kCMajor, -1); // C and D both filled
		const int32_t y = kKeys.y + kKeys.h - 5 + kTop;
		expect(r.lit(kKeys.x + 16, y)).to_be_true();      // their shared edge
		expect(r.lit(kKeys.x + 16 + 1, y)).to_be_false();  // a gap inside D's outline
	});

	it("stays inside its rect", _ {
		for (const Rect a : {Rect{8, 15, 112, 20}, Rect{20, 15, 70, 20}, Rect{20, 15, 14, 6}, Rect{20, 15, 3, 2}}) {
			uitest::Rig48 r;
			octave(r.frame, a, kCMajor, 0);
			for (int32_t y = 0; y < 43; y++) {
				for (int32_t x = 0; x < 128; x++) {
					const bool in = x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h;
					if (!in) {
						expect(r.lit(x, y + kTop)).to_be_false();
					}
				}
			}
		}
	});

	it("draws an empty scale as outlines only", _ {
		uitest::Rig48 r;
		octave(r.frame, kKeys, 0, 0);
		expect(r.lit(kKeys.x + 2 * 16 + 8, kKeys.y + kKeys.h - 5 + kTop)).to_be_false();
		expect(r.lit(kKeys.x, kKeys.y + kKeys.h - 5 + kTop)).to_be_true();
	});

	it("underlines the root", _ {
		uitest::Rig48 r;
		octave(r.frame, kKeys, kCMinor, 7); // G: the fifth white key
		// on r's last row, under a blank row: the keys are two rows shorter than r
		expect(r.lit(kKeys.x + 4 * 16 + 8, kKeys.y + kKeys.h - 1 + kTop)).to_be_true();
		expect(r.lit(kKeys.x + 8, kKeys.y + kKeys.h - 1 + kTop)).to_be_false();
		expect(r.lit(kKeys.x + 4 * 16 + 8, kKeys.y + kKeys.h - 2 + kTop)).to_be_false();
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		octave(r48.frame, kKeys, kCMinor, 0);
		expect_golden(self, "ui/widgets/octave_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		octave(r64.frame, {8, 16, 113, 36}, kCMinor, 0);
		expect_golden(self, "ui/widgets/octave_64.pbm", r64.pbm());
	});
});

describe zone_strip_w("ui::widgets::oled::zone_strip", $ {
	it("fills the edited zone and outlines the rest", _ {
		uitest::Rig48 r;
		const std::array<uint8_t, 3> tops{47, 71, 127};
		zone_strip(r.frame, {0, 27, 128, 12}, tops, 1);
		expect(r.lit(60, 33 + kTop)).to_be_true();  // inside zone 1 (48..71)
		expect(r.lit(20, 33 + kTop)).to_be_false(); // inside zone 0
		expect(r.lit(0, 33 + kTop)).to_be_true();   // zone 0's outline
	});

	it("draws one zone across the keyboard", _ {
		uitest::Rig48 r;
		const std::array<uint8_t, 1> tops{127};
		zone_strip(r.frame, {0, 27, 128, 12}, tops, -1);
		expect(r.lit(0, 30 + kTop)).to_be_true();
		expect(r.lit(127, 30 + kTop)).to_be_true();
	});

	it("caps zone tops at 127 and skips a zone below the one before", _ {
		uitest::Rig48 r;
		const Rect a{0, 27, 64, 12};
		const std::array<uint8_t, 4> tops{60, 200, 30, 127};
		zone_strip(r.frame, a, tops, -1);
		for (int32_t y = 0; y < 43; y++) {
			for (int32_t x = 64; x < 128; x++) {
				expect(r.lit(x, y + kTop)).to_be_false();
			}
		}
	});

	it("stays inside a rect too short for its inset", _ {
		uitest::Rig48 r;
		const std::array<uint8_t, 2> tops{60, 127};
		zone_strip(r.frame, {0, 27, 128, 2}, tops, 0);
		for (int32_t y = 0; y < 43; y++) {
			if (y == 27 || y == 28) {
				continue;
			}
			for (int32_t x = 0; x < 128; x++) {
				expect(r.lit(x, y + kTop)).to_be_false();
			}
		}
	});

	it("draws only the octave ticks for no zones", _ {
		uitest::Rig48 r;
		zone_strip(r.frame, {0, 27, 128, 12}, {}, -1);
		for (int32_t x = 0; x < 128; x++) {
			const bool tick = (x * 128 / 128) % 12 == 0;
			for (int32_t y = 27; y < 39; y++) {
				if (!tick) {
					expect(r.lit(x, y + kTop)).to_be_false();
				}
			}
		}
		expect(r.lit(12, 27 + kTop)).to_be_true();
	});

	it("matches its goldens", _ {
		const std::array<uint8_t, 5> tops{35, 47, 59, 71, 127};
		uitest::Rig48 r48;
		zone_strip(r48.frame, {0, 27, 128, 12}, tops, 3);
		expect_golden(self, "ui/widgets/zone_strip_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		zone_strip(r64.frame, {0, 40, 128, 18}, tops, 3);
		expect_golden(self, "ui/widgets/zone_strip_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(keys_spec, octave_w, zone_strip_w)
