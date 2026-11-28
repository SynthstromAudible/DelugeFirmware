#include "ui/views/oled/detail/layout.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <string_view>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;
using gfx::oled::kTextSpacingX;

// clang-format off
describe text_rows_v("ui::views::oled::detail::text_rows", $ {
	it("clips the left text short of the right", _ {
		uitest::Rig48 r;
		const std::array<RowText, 1> rows{{{"A VERY LONG SAMPLE NAME", "C#3"}}};
		detail::text_rows(r.frame, rows);
		const int32_t right_x = 128 - 3 * kTextSpacingX;
		const int32_t y = detail::row_y(r.frame, 0);
		expect(lit_in(r, right_x - 4, y, 4, r.style.row_height)).to_equal(0);
		expect(lit_in(r, 0, y, right_x - 4, r.style.row_height)).to_be_greater_than(0);
		expect(lit_in(r, right_x, y, 3 * kTextSpacingX, r.style.row_height)).to_be_greater_than(0);
	});

	it("draws nothing for empty text", _ {
		uitest::Rig48 r;
		const std::array<RowText, 2> rows{{{"", ""}, {"", ""}}};
		detail::text_rows(r.frame, rows);
		expect(lit_count(r)).to_equal(0);
	});

	it("places rows from the style", _ {
		uitest::Rig48 r48;
		expect(detail::row_y(r48.frame, 0)).to_equal(r48.style.title_height + 3);
		expect(detail::row_y(r48.frame, 1) - detail::row_y(r48.frame, 0)).to_equal(r48.style.row_height);
		uitest::Rig64 r64;
		expect(detail::row_y(r64.frame, 0)).to_equal(r64.style.title_height + 3);
		expect(detail::row_y(r64.frame, 1) - detail::row_y(r64.frame, 0)).to_equal(r64.style.row_height);
	});

	it("starts a name too wide for the frame at the left edge", _ {
		constexpr std::string_view kLong = "SATURDAY JAM 2026 V3";
		uitest::Rig48 a;
		detail::name_centred(a.frame, 17, kLong);
		uitest::Rig48 b;
		b.frame.glyphs(kLong, 0, 17, gfx::oled::kTextTitleSpacingX, gfx::oled::kTextTitleSizeY);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("starts a row too wide for the frame at the left edge", _ {
		constexpr std::string_view kLong = "C3 EB3 G3 BB3 D4 F4 A4 C5";
		uitest::Rig48 a;
		detail::text_centred(a.frame, 35, kLong);
		uitest::Rig48 b;
		b.frame.glyphs(kLong, 0, 35, kTextSpacingX, gfx::oled::kTextSpacingY);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("shrinks a big value too wide for the frame", _ {
		uitest::Rig48 r;
		detail::big_text(r.frame, "CM7#11/G13 ADD9");
		const int32_t y = detail::row_y(r.frame, 0) - 1;
		expect(lit_in(r, 0, y, 1, 20)).to_equal(0);
		expect(lit_in(r, 127, y, 1, 20)).to_equal(0);
		expect(lit_in(r, 1, y, 126, 20)).to_be_greater_than(0);
	});

	it("draws only the rows that fit", _ {
		uitest::Rig64 r;
		const std::array<RowText, 6> rows{{{"A", "1"}, {"B", "2"}, {"C", "3"}, {"D", "4"}, {"E", "5"}, {"F", "6"}}};
		detail::text_rows(r.frame, rows);
		expect(lit_in(r, 0, 60, 128, 4)).to_equal(0);  // row 5 (y 60) would be cut off
		expect(lit_in(r, 0, 51, 128, 9)).to_be_greater_than(0); // row 4 fits
	});

	it("gives the body the rest of the frame", _ {
		uitest::Rig48 r48;
		const auto b48 = detail::body_rect(r48.frame, 1);
		expect(b48.x).to_equal(0);
		expect(b48.w).to_equal(128);
		expect(b48.y).to_equal(detail::row_y(r48.frame, 1));
		expect(b48.y + b48.h).to_equal(43);
		uitest::Rig64 r64;
		const auto b64 = detail::body_rect(r64.frame, 1);
		expect(b64.y).to_equal(detail::row_y(r64.frame, 1));
		expect(b64.y + b64.h).to_equal(64);
	});
});
// clang-format on

DELUGE_SPEC(layout_spec, text_rows_v)
