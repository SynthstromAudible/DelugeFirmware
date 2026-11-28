#include "support/frames.h"
#include "ui/widgets/grid/scroll_transition.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <optional>

using namespace deluge::ui;
using namespace deluge::ui::widgets::grid;
using deluge::ui::gfx::grid::GridDimensions;
using deluge::ui::gfx::grid::PadGrid;
namespace uitest = deluge::ui::test;

// The scroll `PadLEDs::horizontal::renderScroll` draws, as a component: each step is checked against that function
// transcribed over an image, step for step, in both directions.

namespace {

constexpr GridDimensions kMain{16, 0, 8};

/// A frame with something distinct on every pad, so a step that read the wrong grid or column shows.
PadGrid pattern(uint8_t salt) {
	PadGrid g(kMain);
	for (uint8_t y = 0; y < kMain.rows; y++) {
		for (uint8_t x = 0; x < kMain.main_cols; x++) {
			g.set(x, y, RGB{static_cast<uint8_t>(16 * x + salt), static_cast<uint8_t>(32 * y), salt});
		}
	}
	return g;
}

/// `renderScroll`, transcribed: `image` starts as what the pads showed, `store` holds the new view, and each call is
/// one step of `squaresScrolled`.
struct LegacyScroll {
	std::array<std::array<RGB, 16>, 8> image{};
	std::array<std::array<RGB, 16>, 8> store{};
	int32_t direction = 1;
	int32_t squaresScrolled = 0;
	void step() {
		constexpr int32_t areaToScroll = 16;
		squaresScrolled++;
		const int32_t copyCol = (direction > 0) ? squaresScrolled - 1 : areaToScroll - squaresScrolled;
		const int32_t startSquare = (direction > 0) ? 0 : areaToScroll - 1;
		const int32_t endSquare = (direction > 0) ? areaToScroll - 1 : 0;
		for (int32_t row = 0; row < 8; row++) {
			for (int32_t x = startSquare; x != endSquare; x += direction) {
				image[row][x] = image[row][x + direction];
			}
			image[row][endSquare] = store[row][copyCol];
		}
	}
};

/// Run the widget against the transcription for every step of one direction.
bool matches_legacy(int8_t direction) {
	const PadGrid from = pattern(1);
	const PadGrid to = pattern(77);
	LegacyScroll legacy;
	legacy.direction = direction;
	for (uint8_t y = 0; y < 8; y++) {
		for (uint8_t x = 0; x < 16; x++) {
			legacy.image[y][x] = from.at(x, y);
			legacy.store[y][x] = to.at(x, y);
		}
	}
	ScrollTransition s;
	begin_scroll(s, from, direction, 1000) = to;
	for (int32_t k = 1; k < 16; k++) {
		legacy.step();
		uitest::GridRig r;
		r.begin_tick(1000 + static_cast<uint32_t>(k - 1) * kScrollStepMs);
		if (!scroll_transition(r.frame, s)) {
			return false;
		}
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 16; x++) {
				if (!(r.grid.at(x, y) == legacy.image[y][x])) {
					return false;
				}
			}
		}
	}
	return true;
}

} // namespace

// clang-format off
describe scroll_transition_grid("ui::widgets::grid::scroll_transition", $ {
	it("slides the view right one column a step, as renderScroll does", _ {
		expect(matches_legacy(1)).to_equal(true);
	});

	it("slides the view left one column a step, as renderScroll does", _ {
		expect(matches_legacy(-1)).to_equal(true);
	});

	it("draws its first step at once, asks for the next a refresh later, and ends with the sixteenth", _ {
		const PadGrid from = pattern(1);
		const PadGrid to = pattern(77);
		ScrollTransition s;
		uitest::GridRig r;
		r.begin_tick(500);
		begin_scroll(s, from, 1, 500) = to;
		expect(scroll_transition(r.frame, s)).to_equal(true);
		expect(r.gate.next_deadline()).to_equal(std::optional<uint32_t>{500 + kScrollStepMs});
		// The sidebar is the owner's.
		expect(r.grid.at(16, 0)).to_equal(RGB{});
		r.begin_tick(500 + 14 * kScrollStepMs);
		expect(scroll_transition(r.frame, s)).to_equal(true);
		r.begin_tick(500 + 15 * kScrollStepMs);
		expect(scroll_transition(r.frame, s)).to_equal(false);
		expect(s.active).to_equal(false);
	});

	it("paints nothing before a scroll begins", _ {
		ScrollTransition s;
		uitest::GridRig r;
		r.begin_tick(0);
		expect(scroll_transition(r.frame, s)).to_equal(false);
		expect(r.gate.next_deadline().has_value()).to_equal(false);
	});
});
// clang-format on

DELUGE_SPEC(scroll_transition_spec, scroll_transition_grid)
