#include "support/frames.h"
#include "ui/widgets/oled/scope.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

namespace {

constexpr int32_t kTop = 5; // the 48-row rig's hidden rows: canvas row = local row + 5

/// No lit pixel outside @p r (local coordinates on the 48-row rig).
bool inside(uitest::Rig48& r, Rect a) {
	for (int32_t y = 0; y < 43; y++) {
		for (int32_t x = 0; x < 128; x++) {
			const bool in = x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h;
			if (!in && r.lit(x, y + kTop)) {
				return false;
			}
		}
	}
	return true;
}

/// The canvas sketch's master mix: two partials and a seventh harmonic, one cycle per 32 columns.
std::vector<int8_t> mix(int32_t n) {
	std::vector<int8_t> s(static_cast<size_t>(n));
	for (int32_t x = 0; x < n; x++) {
		const float u = static_cast<float>(x) / 32.f * 6.2831853f;
		const float v = 0.62f * std::sin(u) + 0.28f * std::sin(2 * u + 0.7f) + 0.12f * std::sin(7 * u + 0.3f);
		s[static_cast<size_t>(x)] = static_cast<int8_t>(std::lround(v * 120.f));
	}
	return s;
}

} // namespace

// clang-format off
describe scope_w("ui::widgets::oled::scope", $ {
	it("draws silence on the centre row", _ {
		uitest::Rig48 r;
		const std::vector<int8_t> silence(128, 0);
		scope(r.frame, {0, 14, 128, 28}, silence);
		for (int32_t x = 0; x < 128; x++) {
			expect(r.lit(x, 14 + 14 + kTop)).to_be_true();
		}
	});

	it("clamps full scale to the rect", _ {
		uitest::Rig48 r;
		const std::array<int8_t, 4> edges{127, -128, 127, -128};
		const Rect a{10, 14, 60, 28};
		scope(r.frame, a, edges);
		expect(inside(r, a)).to_be_true();
		expect(r.lit(11, 14 + 27 + kTop)).to_be_true(); // -128 reaches the bottom row
	});

	it("draws nothing past the rect's width", _ {
		uitest::Rig48 r;
		const std::vector<int8_t> many = mix(300);
		const Rect a{0, 14, 100, 28};
		scope(r.frame, a, many);
		expect(inside(r, a)).to_be_true();
	});

	it("draws only the centre line for no samples", _ {
		uitest::Rig48 r;
		scope(r.frame, {0, 14, 128, 28}, {});
		expect(r.lit(0, 28 + kTop)).to_be_true();
		expect(r.lit(1, 28 + kTop)).to_be_false();
		expect(r.lit(2, 28 + kTop)).to_be_true();
		expect(r.lit(0, 27 + kTop)).to_be_false();
	});

	it("matches its goldens", _ {
		const std::vector<int8_t> s = mix(128);
		uitest::Rig48 r48;
		scope(r48.frame, {0, 14, 128, 28}, s);
		expect_golden(self, "ui/widgets/scope_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		scope(r64.frame, {0, 16, 128, 46}, s);
		expect_golden(self, "ui/widgets/scope_64.pbm", r64.pbm());
	});
});

describe scope_xy_w("ui::widgets::oled::scope_xy", $ {
	it("draws a mono signal on the diagonal", _ {
		uitest::Rig48 r;
		const std::array<int8_t, 1> s{64};
		scope_xy(r.frame, {0, 14, 128, 28}, s, s);
		// box 56 wide, 28 tall, centred: x 36..91, centre (64, 28); 64 is a quarter of the way out on each axis
		expect(r.lit(64 + 14, 28 - 7 + kTop)).to_be_true();
	});

	it("uses the shorter of left and right", _ {
		uitest::Rig48 r;
		uitest::Rig48 one;
		const std::array<int8_t, 3> left{64, -64, 32};
		const std::array<int8_t, 1> right{64};
		scope_xy(r.frame, {0, 14, 128, 28}, left, right);
		scope_xy(one.frame, {0, 14, 128, 28}, std::span<const int8_t>{left}.first(1), right);
		expect(r.pbm()).to_equal(one.pbm());
	});

	it("matches its goldens", _ {
		std::vector<int8_t> l(64);
		std::vector<int8_t> rr(64);
		for (int32_t i = 0; i < 64; i++) {
			const float u = static_cast<float>(i) / 64.f * 6.2831853f;
			l[static_cast<size_t>(i)] = static_cast<int8_t>(std::lround(110.f * std::sin(u)));
			rr[static_cast<size_t>(i)] = static_cast<int8_t>(std::lround(110.f * std::sin(u + 1.1f)));
		}
		uitest::Rig48 r48;
		scope_xy(r48.frame, {0, 14, 128, 28}, l, rr);
		expect_golden(self, "ui/widgets/scope_xy_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		scope_xy(r64.frame, {0, 16, 128, 46}, l, rr);
		expect_golden(self, "ui/widgets/scope_xy_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(scope_spec, scope_w, scope_xy_w)
