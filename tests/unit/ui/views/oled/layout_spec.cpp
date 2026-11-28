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
		const std::array<detail::RowText, 1> rows{{{"A VERY LONG SAMPLE NAME", "C#3"}}};
		detail::text_rows(r.frame, rows);
		const int32_t right_x = 128 - 3 * kTextSpacingX;
		const int32_t y = detail::row_y(r.frame, 0);
		expect(lit_in(r, right_x - 4, y, 4, r.style.row_height)).to_equal(0);
		expect(lit_in(r, 0, y, right_x - 4, r.style.row_height)).to_be_greater_than(0);
		expect(lit_in(r, right_x, y, 3 * kTextSpacingX, r.style.row_height)).to_be_greater_than(0);
	});

	it("draws nothing for empty text", _ {
		uitest::Rig48 r;
		const std::array<detail::RowText, 2> rows{{{"", ""}, {"", ""}}};
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
