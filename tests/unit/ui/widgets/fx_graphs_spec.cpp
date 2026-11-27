#include "support/frames.h"
#include "ui/widgets/oled/fx_graphs.h"
#include "ui/widgets/oled/page_drawing.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <cmath>
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
namespace uitest = deluge::ui::test;

namespace {

constexpr int32_t kTop = 5; // the 48-row rig's hidden rows: canvas row = local row + 5

/// The duck's columns, as `duck_graph` documents them.
struct DuckX {
	int32_t trigger, attack_end, release_end;
};
DuckX duck_x(const PageBody& b, float attack, float release) {
	const int32_t xt = b.left + 4;
	const int32_t xa = xt + static_cast<int32_t>(std::lround(attack * static_cast<float>(b.right - b.left) / 4.f));
	const int32_t xr = xa + static_cast<int32_t>(std::lround(release * static_cast<float>(b.right - xa - 4)));
	return {xt, xa, xr};
}

/// No lit pixel outside the body grown by @p grow on every side.
bool inside(uitest::Rig48& r, const PageBody& b, int32_t grow) {
	for (int32_t y = 0; y < 43; y++) {
		for (int32_t x = 0; x < 128; x++) {
			const bool in = x >= b.left - grow && x <= b.right + grow && y >= b.top - grow && y <= b.bottom + grow;
			if (!in && r.lit(x, y + kTop)) {
				return false;
			}
		}
	}
	return true;
}

} // namespace

// clang-format off
describe duck_graph_w("ui::widgets::oled::duck_graph", $ {
	it("draws the trigger guide and a flat line at rest when volume is 0", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		duck_graph(r.frame, {.volume = 0.f, .shape = 0.5f, .attack = 0.5f, .release = 0.5f}, {}, -1);
		const DuckX x = duck_x(b, 0.5f, 0.5f);
		// guide_v dots rows b.top - 1 + 4k
		expect(r.lit(x.trigger, b.top + 3 + kTop)).to_be_true();
		for (int32_t c = b.left; c <= b.right; c++) {
			expect(r.lit(c, b.y_at(1.f) + kTop)).to_be_true();
		}
	});

	it("falls to the depth over the attack and recovers to rest by the release's end", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		duck_graph(r.frame, {.volume = -0.8f, .shape = 0.5f, .attack = 0.4f, .release = 0.6f}, {}, -1);
		const DuckX x = duck_x(b, 0.4f, 0.6f);
		expect(r.lit(x.attack_end, b.y_at(0.2f) + kTop)).to_be_true();
		expect(r.lit(x.release_end, b.y_at(1.f) + kTop)).to_be_true();
		expect(r.lit(x.attack_end, b.y_at(1.f) + kTop)).to_be_false();
	});

	it("rises for a positive volume", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		duck_graph(r.frame, {.volume = 0.8f, .shape = 0.5f, .attack = 0.4f, .release = 0.6f}, {}, -1);
		const DuckX x = duck_x(b, 0.4f, 0.6f);
		expect(r.lit(x.attack_end, b.y_at(1.f) + kTop)).to_be_true();
		expect(r.lit(b.left, b.y_at(0.2f) + kTop)).to_be_true();
	});

	it("inverts the focused handle's centre and outlines the others", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		duck_graph(r.frame, {.volume = -0.8f, .shape = 0.5f, .attack = 0.4f, .release = 0.6f},
		           {.volume = 0, .shape = 1, .attack = 2, .release = 3}, 2);
		const DuckX x = duck_x(b, 0.4f, 0.6f);
		const int32_t ya = b.y_at(0.2f) + kTop;
		const int32_t yr = b.y_at(1.f) + kTop;
		expect(r.lit(x.attack_end, ya)).to_be_true();     // focused: interior inverted
		expect(r.lit(x.release_end, yr)).to_be_false();   // unfocused: interior cleared
		expect(r.lit(x.attack_end - 2, ya - 2)).to_be_true();
		expect(r.lit(x.release_end - 2, yr - 2)).to_be_true();
	});

	it("keeps the focused handle on top when attack and release are 0", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		duck_graph(r.frame, {.volume = -1.f, .shape = 0.f, .attack = 0.f, .release = 0.f},
		           {.attack = 2, .release = 3}, 2);
		const DuckX x = duck_x(b, 0.f, 0.f);
		expect(r.lit(x.attack_end, b.y_at(0.f) + kTop)).to_be_true();
	});

	it("clamps out-of-range values", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		duck_graph(r.frame, {.volume = -3.f, .shape = 2.f, .attack = -1.f, .release = 5.f},
		           {.volume = 0, .shape = 1, .attack = 2, .release = 3}, 1);
		expect(inside(r, b, 2)).to_be_true();
	});

	it("rides a dot along the gain at the phase", _ {
		uitest::Rig48 plain;
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		const DuckGraph d{.volume = -0.8f, .shape = 0.5f, .attack = 0.2f, .release = 0.8f};
		duck_graph(plain.frame, d, {}, -1);
		duck_graph(r.frame, d, {}, -1, 0.5f);
		const int32_t x = b.left + static_cast<int32_t>(std::lround(0.5f * static_cast<float>(b.right - b.left)));
		int32_t y = -1;
		for (int32_t row = b.top; row <= b.bottom && y < 0; row++) {
			if (plain.lit(x, row + kTop)) {
				y = row;
			}
		}
		expect(y).to_be_greater_than(-1);
		// a full 3x3 block within two rows of the trace, where the plain render has none
		bool found = false;
		for (int32_t cy = y - 2; cy <= y + 2 && !found; cy++) {
			bool full = true, plain_full = true;
			for (int32_t dx = -1; dx <= 1; dx++) {
				for (int32_t dy = -1; dy <= 1; dy++) {
					full = full && r.lit(x + dx, cy + dy + kTop);
					plain_full = plain_full && plain.lit(x + dx, cy + dy + kTop);
				}
			}
			found = full && !plain_full;
		}
		expect(found).to_be_true();
	});

	it("matches its goldens", _ {
		const DuckGraph duck{.volume = -0.7f, .shape = 0.6f, .attack = 0.3f, .release = 0.7f};
		const DuckHandles all{.volume = 0, .shape = 1, .attack = 2, .release = 3};
		uitest::Rig48 r48;
		duck_graph(r48.frame, duck, all, 1);
		expect_golden(self, "ui/widgets/fx_duck_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		duck_graph(r64.frame, duck, all, 1);
		expect_golden(self, "ui/widgets/fx_duck_64.pbm", r64.pbm());

		const DuckGraph rise{.volume = 0.6f, .shape = 0.3f, .attack = 0.2f, .release = 0.5f};
		const DuckHandles two{.attack = 0, .release = 1};
		uitest::Rig48 q48;
		duck_graph(q48.frame, rise, two, 0);
		expect_golden(self, "ui/widgets/fx_duck_rise_48.pbm", q48.pbm());
		uitest::Rig64 q64;
		duck_graph(q64.frame, rise, two, 0);
		expect_golden(self, "ui/widgets/fx_duck_rise_64.pbm", q64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(fx_graphs_spec, duck_graph_w)
