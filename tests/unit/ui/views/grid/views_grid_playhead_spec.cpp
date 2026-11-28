#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/playhead.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/grid/support/flashes.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace deluge::ui::views::grid;
using deluge::ui::widgets::grid::PadFlash;
using deluge::ui::widgets::grid::TickColour;
using deluge::ui::widgets::grid::TickSquare;
namespace uitest = deluge::ui::test;
using deluge::ui::views::grid::test::flashes_of;

namespace {

/// Every row's square at column @p col.
Playhead at_column(uint8_t col, bool paint, bool flash, bool fast) {
	Playhead d{.per_row = {}, .paint = paint, .muted = deluge::gui::colours::black, .flash = flash, .fast = fast};
	d.per_row.fill(TickSquare{.col = col, .colour = TickColour::Normal});
	return d;
}

} // namespace

// clang-format off
describe playhead_v("ui::views::grid::playhead", $ {
	it("paints the column on every row with the cursor Slow, and matches its golden", _ {
		uitest::GridRig r;
		std::array<uint8_t, 8> last{};
		last.fill(deluge::ui::widgets::grid::kNoTickSquare);
		playhead(r.frame, at_column(5, true, true, false), last);
		const RGB tick = deluge::ui::widgets::grid::tick_colour(TickColour::Normal, RGB{});
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(5, y)).to_equal(tick);
			expect(static_cast<int32_t>(last[static_cast<size_t>(y)])).to_equal(5);
		}
		expect(flashes_of(r)).to_equal(std::vector<PadFlash>{});
		expect_golden(self, "ui/views/grid/playhead_slow.state", r.state());
	});

	it("flashes each row's square Fast, and nothing on a second call with the same latch", _ {
		std::array<uint8_t, 8> last{};
		last.fill(deluge::ui::widgets::grid::kNoTickSquare);
		uitest::GridRig first;
		playhead(first.frame, at_column(5, false, true, true), last);
		expect(flashes_of(first).size()).to_equal(size_t{8});
		expect(first.grid.at(5, 0)).to_equal(deluge::gui::colours::black);
		uitest::GridRig second;
		playhead(second.frame, at_column(5, false, true, true), last);
		expect(flashes_of(second)).to_equal(std::vector<PadFlash>{});
	});

	it("leaves the latch alone with the cursor Off", _ {
		std::array<uint8_t, 8> last{};
		last.fill(deluge::ui::widgets::grid::kNoTickSquare);
		uitest::GridRig r;
		playhead(r.frame, at_column(5, false, false, false), last);
		expect(static_cast<int32_t>(last[0])).to_equal(static_cast<int32_t>(deluge::ui::widgets::grid::kNoTickSquare));
		expect(r.grid.at(5, 0)).to_equal(deluge::gui::colours::black);
		expect(flashes_of(r)).to_equal(std::vector<PadFlash>{});
	});
});
// clang-format on

DELUGE_SPEC(views_grid_playhead_spec, playhead_v)
