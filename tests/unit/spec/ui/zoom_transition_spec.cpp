#include "gfx/grid/tween.h"
#include "support/frames.h"
#include "ui/grid/zoom_transition.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <cstdint>
#include <optional>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::GridDimensions;
using deluge::gfx::grid::PadGrid;
using deluge::gfx::grid::ZoomParams;
namespace uitest = deluge::ui::test;

// The slide `PadLEDs::renderZoom` draws from `imageStore` (`pad_leds.cpp:1195`), as a component. The blend itself is
// `gfx::grid::zoom`'s, pinned in tests/unit/spec/gfx/tween_spec.cpp; what is pinned here is the clock around it.

namespace {

constexpr GridDimensions kMain{16, 0, 8};

/// A frame with something distinct on every pad, so a step that read the wrong grid shows.
PadGrid pattern(uint8_t salt) {
	PadGrid g(kMain);
	for (uint8_t y = 0; y < kMain.rows; y++) {
		for (uint8_t x = 0; x < kMain.main_cols; x++) {
			g.set(x, y, RGB{static_cast<uint8_t>(16 * x + salt), static_cast<uint8_t>(32 * y), salt});
		}
	}
	return g;
}

/// Zooming in one step about square 5.
constexpr ZoomParams kIn{
    .old_zoom = 3000, .new_zoom = 2048, .pin_square_from = 5 << 16, .pin_square_to = 5 << 16, .t = 0, .magnitude = 1};

bool shows(const uitest::GridRig& r, const PadGrid& g) {
	for (uint8_t y = 0; y < kMain.rows; y++) {
		for (uint8_t x = 0; x < kMain.main_cols; x++) {
			if (!(r.grid.at(x, y) == g.at(x, y))) {
				return false;
			}
		}
	}
	return true;
}

} // namespace

// clang-format off
describe zoom_transition_spec("ui/grid/zoom_transition", $ {
	it("paints the zoom's step at the clock's progress, every 50 ms, until 110 ms have passed", _ {
		const PadGrid from = pattern(1);
		const PadGrid to = pattern(77);
		ZoomTransition z;
		uitest::GridRig r;
		r.begin_tick(1000);
		begin_zoom(z, from, kIn, 1000) = to;

		// `getTransitionProgress`: (now - start) * 65536 / length, here 55 * 65536 / 110 = 32768.
		r.begin_tick(1055);
		expect(zoom_transition(r.frame, z)).to_be_true();
		PadGrid half(kMain);
		ZoomParams at_half = kIn;
		at_half.t = 32768;
		deluge::gfx::grid::zoom(from, to, at_half, half);
		expect(shows(r, half)).to_be_true();
		expect(r.gate.next_deadline() == std::optional<uint32_t>{1105}).to_be_true();
		// The sidebar is the owner's.
		expect(r.grid.at(16, 0) == RGB{}).to_be_true();

		r.grid.clear();
		r.begin_tick(1109);
		expect(zoom_transition(r.frame, z)).to_be_true();
		r.grid.clear();
		r.begin_tick(1110);
		expect(zoom_transition(r.frame, z)).to_be_false();
		expect(z.active).to_be_false();
		expect(r.grid.at(5, 3) == RGB{}).to_be_true();
		r.begin_tick(1111);
		expect(zoom_transition(r.frame, z)).to_be_false();
	});

	it("starts from what the pads showed", _ {
		const PadGrid from = pattern(1);
		const PadGrid to = pattern(77);
		ZoomTransition z;
		uitest::GridRig r;
		r.begin_tick(0);
		begin_zoom(z, from, kIn, 0) = to;
		expect(zoom_transition(r.frame, z)).to_be_true();
		PadGrid start(kMain);
		deluge::gfx::grid::zoom(from, to, kIn, start);
		expect(shows(r, start)).to_be_true();
		expect(shows(r, to)).to_be_false();
	});

	it("paints nothing before a zoom begins", _ {
		ZoomTransition z;
		uitest::GridRig r;
		r.begin_tick(0);
		expect(zoom_transition(r.frame, z)).to_be_false();
		expect(r.gate.next_deadline().has_value()).to_be_false();
	});
});
// clang-format on
DELUGE_SPEC(ui_zoom_transition_spec, zoom_transition_spec)
