#include "ui/views/oled/detail/layout.h"
#include "ui/views/oled/master.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

/// The canvas sketch's master mix (as `scope_spec.cpp`'s): two partials and a seventh harmonic, a cycle per 32.
std::vector<int8_t> mix(int32_t n) {
	std::vector<int8_t> s(static_cast<size_t>(n));
	for (int32_t x = 0; x < n; x++) {
		const float u = static_cast<float>(x) / 32.f * 6.2831853f;
		const float v = 0.62f * std::sin(u) + 0.28f * std::sin(2 * u + 0.7f) + 0.12f * std::sin(7 * u + 0.3f);
		s[static_cast<size_t>(x)] = static_cast<int8_t>(std::lround(v * 120.f));
	}
	return s;
}

/// L and R a quarter-ish cycle apart: an ellipse (as `scope_spec.cpp`'s).
void ellipse(std::vector<int8_t>& l, std::vector<int8_t>& r) {
	l.resize(64);
	r.resize(64);
	for (int32_t i = 0; i < 64; i++) {
		const float u = static_cast<float>(i) / 64.f * 6.2831853f;
		l[static_cast<size_t>(i)] = static_cast<int8_t>(std::lround(110.f * std::sin(u)));
		r[static_cast<size_t>(i)] = static_cast<int8_t>(std::lround(110.f * std::sin(u + 1.1f)));
	}
}

} // namespace

// clang-format off
describe master_scope_v("ui::views::oled::master_scope", $ {
	it("draws the centre line with no samples", _ {
		uitest::Rig48 r;
		master_scope(r.frame, {.title = "MASTER", .samples = {}});
		const auto b = detail::body_rect(r.frame, 0);
		const int32_t cy = b.y + b.h / 2;
		expect(r.lit(0, cy + kTop)).to_be_true();
		expect(r.lit(1, cy + kTop)).to_be_false();
		expect(lit_in(r, b.x, b.y, b.w, b.h)).to_equal(64);
	});

	it("matches its goldens", _ {
		const auto s = mix(128);
		uitest::Rig48 r48;
		master_scope(r48.frame, {.title = "MASTER", .samples = s});
		expect_golden(self, "ui/views/oled/master_scope_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		master_scope(r64.frame, {.title = "MASTER", .samples = s});
		expect_golden(self, "ui/views/oled/master_scope_64.pbm", r64.pbm());
	});
});

describe master_scope_xy_v("ui::views::oled::master_scope_xy", $ {
	it("draws the axes with no samples", _ {
		uitest::Rig48 r;
		master_scope_xy(r.frame, {.title = "MASTER", .note = "X/Y", .left = {}, .right = {}});
		const auto b = detail::body_rect(r.frame, 0);
		expect(lit_in(r, b.x, b.y, b.w, b.h)).to_be_greater_than(0);
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		std::vector<int8_t> l;
		std::vector<int8_t> rr;
		ellipse(l, rr);
		const MasterScopeXy d{.title = "MASTER", .note = "X/Y", .left = l, .right = rr};
		uitest::Rig48 r48;
		master_scope_xy(r48.frame, d);
		expect_golden(self, "ui/views/oled/master_scope_xy_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		master_scope_xy(r64.frame, d);
		expect_golden(self, "ui/views/oled/master_scope_xy_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(master_spec, master_scope_v, master_scope_xy_v)
