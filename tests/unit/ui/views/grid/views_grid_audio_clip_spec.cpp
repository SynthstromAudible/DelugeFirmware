#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/audio_clip.h"
#include "ui/views/grid/clip_sidebar.h"
#include "ui/views/grid/scroll_slide.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/scroll_transition.h"
#include "ui/widgets/grid/sidebar.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <array>
#include <cstdint>

using namespace deluge::ui::views::grid;
using deluge::ui::widgets::grid::GridFrame;
using deluge::ui::widgets::grid::MacroCell;
using deluge::ui::widgets::grid::MacroKind;
using deluge::ui::widgets::grid::ScrollTransition;
namespace colours = deluge::gui::colours;
namespace uitest = deluge::ui::test;

namespace {

constexpr deluge::ui::gfx::grid::Rect kMain{0, 0, 16, 8};

void draw(uitest::GridRig& r, const ClipMarkers& d) {
	r.frame.region(kMain, [&](GridFrame& main) { clip_markers(main, d); });
}

} // namespace

// clang-format off
describe clip_markers_v("ui::views::grid::clip_markers", $ {
	it("draws the end marker red and the start marker green while lit, greying past the end", _ {
		uitest::GridRig r;
		draw(r, {.start_square = 2, .end_square = 12, .start_visible = true, .end_visible = true, .blink_on = true});
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(2, y)).to_equal(RGB{colours::green});
			expect(r.grid.at(12, y)).to_equal(RGB{colours::red});
			for (int32_t x = 13; x < 16; x++) {
				expect(r.grid.at(x, y)).to_equal(RGB{colours::grey});
			}
			expect(r.grid.at(5, y)).to_equal(RGB{});
		}
		expect(r.grid.at(16, 0)).to_equal(RGB{});
		expect_golden(self, "ui/views/grid/clip_markers_both.state", r.state());
	});

	it("dims both markers while the blink is off", _ {
		uitest::GridRig r;
		draw(r, {.start_square = 2, .end_square = 12, .start_visible = true, .end_visible = true, .blink_on = false});
		expect(r.grid.at(2, 4)).to_equal(RGB{colours::green.dim()});
		expect(r.grid.at(12, 4)).to_equal(RGB{colours::red_dull});
	});

	it("leaves out a marker that is not visible, and still greys past the end", _ {
		uitest::GridRig r;
		draw(r, {.start_square = 2, .end_square = 12, .start_visible = false, .end_visible = false, .blink_on = true});
		expect(r.grid.at(2, 0)).to_equal(RGB{});
		expect(r.grid.at(12, 0)).to_equal(RGB{});
		expect(r.grid.at(13, 0)).to_equal(RGB{colours::grey});
	});

	it("greys the whole width when the start has scrolled off the right", _ {
		uitest::GridRig r;
		draw(r, {.start_square = 16, .end_square = 20, .start_visible = true, .end_visible = true, .blink_on = true});
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 16; x++) {
				expect(r.grid.at(x, y)).to_equal(RGB{colours::grey});
			}
		}
		expect_golden(self, "ui/views/grid/clip_markers_start_off.state", r.state());
	});
});

describe scroll_slide_v("ui::views::grid::scroll_slide", $ {
	it("paints nothing and answers false with no slide running", _ {
		uitest::GridRig r;
		ScrollTransition s;
		expect(scroll_slide(r.frame, s)).to_be_false();
		expect(r.grid.at(5, 3)).to_equal(RGB{});
	});
});

describe clip_sidebar_v("ui::views::grid::clip_sidebar", $ {
	it("paints both sidebar columns black without macros", _ {
		uitest::GridRig r;
		for (int32_t y = 0; y < 8; y++) {
			(void)r.frame.pad(16, y, RGB{colours::red});
			(void)r.frame.pad(17, y, RGB{colours::red});
		}
		clip_sidebar(r.frame, 16, ClipSidebar{.macros = false, .cells = {}});
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(16, y)).to_equal(RGB{});
			expect(r.grid.at(17, y)).to_equal(RGB{});
		}
	});

	it("paints the macro column over the black sidebar as the macros widget does", _ {
		ClipSidebar d{.macros = true, .cells = {}};
		d.cells[0].kind = MacroKind::Section;
		d.cells[3].kind = MacroKind::ClipLaunch;
		d.cells[3].clip_active = true;
		uitest::GridRig r;
		clip_sidebar(r.frame, 16, d);
		uitest::GridRig want;
		const std::array<RGB, 8> black{};
		deluge::ui::widgets::grid::sidebar_columns(want.frame, black, black);
		deluge::ui::widgets::grid::macros(want.frame, 16, d.cells, -1);
		expect(r.state()).to_equal(want.state());
		expect(r.grid.at(16, 3)).not_().to_equal(RGB{});
	});
});
// clang-format on

DELUGE_SPEC(views_grid_audio_clip_spec, clip_markers_v, scroll_slide_v, clip_sidebar_v)
