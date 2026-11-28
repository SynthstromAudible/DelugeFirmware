#include "ui/gfx/oled/icons.h"
#include "ui/views/oled/browser.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

/// Three files and a folder, the second file selected.
constexpr std::array<BrowserRow, 4> kRows{{
    {.label = "CLAP", .folder = false, .selected = false, .scrolls = false},
    {.label = "SNARE", .folder = false, .selected = true, .scrolls = false},
    {.label = "TOM", .folder = false, .selected = false, .scrolls = false},
    {.label = "KICKS", .folder = true, .selected = false, .scrolls = false},
}};

/// The list over @p rows, as many as @p menu_rows has room for, with "SNARE" being edited.
BrowserList list_of(std::span<const BrowserRow> rows, int32_t menu_rows, std::string_view entered = "SNARE") {
	return {.title = "Audio files",
	        .file_icon = &gfx::oled::icons::browserWave,
	        .file_icon_pt2 = nullptr,
	        .entered = entered,
	        .rows = rows.first(std::min(rows.size(), static_cast<size_t>(menu_rows)))};
}

/// The list with the cursor lit at the start of the edited name.
void lit(widgets::oled::OledFrame& f, const BrowserList& d, widgets::core::ScrollText& scroll) {
	widgets::oled::TextEditState edit{};
	edit.cursor_lit = true;
	browser_list(f, d, edit, scroll);
}

void draw(uitest::Rig48& r, std::span<const BrowserRow> rows) {
	widgets::core::ScrollText scroll{};
	lit(r.frame, list_of(rows, r.style.menu_rows), scroll);
}

void draw(uitest::Rig64& r, std::span<const BrowserRow> rows) {
	widgets::core::ScrollText scroll{};
	lit(r.frame, list_of(rows, r.style.menu_rows), scroll);
}

/// The one selected row, its name wider than the row, side-scrolling.
constexpr std::array<BrowserRow, 1> kLong{{
    {.label = "KICK2 WITH A LONG ROOM TAIL", .folder = false, .selected = true, .scrolls = true},
}};

} // namespace

// clang-format off
describe browser_list_v("ui::views::oled::browser_list", $ {
	it("draws the selected row's edited name rather than its label", _ {
		uitest::Rig48 shown;
		draw(shown, kRows);
		uitest::Rig48 other;
		widgets::core::ScrollText scroll{};
		lit(other.frame, list_of(kRows, other.style.menu_rows, "SNAREX"), scroll);
		expect(other.pbm() == shown.pbm()).to_equal(false);
	});

	it("side-scrolls a selected row that scrolls, and arms the panel's gate for its next step", _ {
		widgets::core::ScrollText scroll{};
		uitest::Rig48 at0;
		at0.begin_tick(0);
		lit(at0.frame, list_of(kLong, at0.style.menu_rows, kLong[0].label), scroll);
		expect(at0.gate.next_deadline().has_value()).to_be_true();
		uitest::Rig48 at600;
		at600.begin_tick(600);
		lit(at600.frame, list_of(kLong, at600.style.menu_rows, kLong[0].label), scroll);
		expect(at600.pbm() == at0.pbm()).to_equal(false);
	});

	it("matches its goldens", _ {
		uitest::Rig48 l48;
		draw(l48, kRows);
		expect_golden(self, "ui/views/oled/browser_list_48.pbm", l48.pbm());
		uitest::Rig64 l64;
		draw(l64, kRows);
		expect_golden(self, "ui/views/oled/browser_list_64.pbm", l64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(browser_spec, browser_list_v)
