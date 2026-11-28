#include "support/frames.h"
#include "ui/widgets/oled/fx_graphs.h"
#include "ui/widgets/oled/page_drawing.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <cmath>
#include <cstdint>
#include <vector>

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

/// The column at fraction @p u of the body's width.
int32_t x_at(const PageBody& b, float u) {
	return b.left + static_cast<int32_t>(std::lround(u * static_cast<float>(b.right - b.left)));
}

/// The rows lit in local column @p x of the 48-row rig, within the body.
std::vector<int32_t> lit_rows(uitest::Rig48& r, const PageBody& b, int32_t x) {
	std::vector<int32_t> rows;
	for (int32_t y = b.top - 2; y <= b.bottom + 2; y++) {
		if (r.lit(x, y + kTop)) {
			rows.push_back(y);
		}
	}
	return rows;
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
		// volume 0, attack 0, release 0: the attack and release handles land on the same pixel
		const DuckGraph d{.volume = 0.f, .shape = 0.f, .attack = 0.f, .release = 0.f};
		const DuckHandles h{.attack = 2, .release = 3};
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		const DuckX x = duck_x(b, 0.f, 0.f);
		expect(x.attack_end).to_equal(x.release_end);
		duck_graph(r.frame, d, h, 2);
		expect(r.lit(x.attack_end, b.y_at(1.f) + kTop)).to_be_true(); // focused: interior inverted
		uitest::Rig48 q;
		duck_graph(q.frame, d, h, 3);
		expect(q.lit(x.attack_end, b.y_at(1.f) + kTop)).to_be_true(); // either way round
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
describe distortion_graph_w("ui::widgets::oled::distortion_graph", $ {
	it("draws the output on the clean cycle's crest with every effect at 0", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		distortion_graph(r.frame, {}, -1);
		const int32_t amp = b.height() / 2 - 1;
		expect(r.lit(x_at(b, 0.25f) + 4, b.center_y() - amp + kTop)).to_be_true(); // clear of the handle at u = 0.25
	});

	it("holds the output flat for the decimation's run", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		distortion_graph(r.frame, {.decimation = 1.f}, -1);
		for (int32_t x = b.left; x <= b.left + 10; x++) {
			expect(r.lit(x, b.center_y() + kTop)).to_be_true();
		}
	});

	it("steps the output to the bitcrush's levels", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		distortion_graph(r.frame, {.bitcrush = 1.f}, -1);
		const int32_t cy = b.center_y();
		const int32_t amp = b.height() / 2 - 1;
		const int32_t levels[] = {cy - amp, cy - amp / 2, cy, cy + amp / 2, cy + amp};
		for (int32_t x = b.left | 1; x <= b.right; x += 2) { // odd columns: off the dotted clean line
			const std::vector<int32_t> rows = lit_rows(r, b, x);
			if (rows.size() != 1) {
				continue; // a step's vertical, or a handle
			}
			int32_t nearest = 99;
			for (int32_t l : levels) {
				nearest = std::min(nearest, std::abs(rows[0] - l));
			}
			expect(nearest).to_be_less_than(2);
		}
	});

	it("folds a peak past the threshold back through the centre", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		distortion_graph(r.frame, {.wavefold = 1.f}, -1);
		const int32_t amp = b.height() / 2 - 1;
		// t = 0.4: a peak of 1 folds to 2 * 0.4 - 1 = -0.2
		const int32_t x = x_at(b, 0.25f) + 4; // clear of the handle at u = 0.25
		expect(r.lit(x, b.center_y() + static_cast<int32_t>(std::lround(0.2f * static_cast<float>(amp))) + kTop))
		    .to_be_true();
		expect(r.lit(x, b.center_y() - amp + kTop)).to_be_false();
	});

	it("inverts the focused handle", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		distortion_graph(r.frame, {.saturation = 0.4f, .bitcrush = 0.3f, .decimation = 0.3f, .wavefold = 0.5f}, 3);
		const float us[] = {0.25f, 0.10f, 0.58f, 0.75f};
		for (int32_t i = 0; i < 4; i++) {
			const int32_t x = x_at(b, us[i]);
			const std::vector<int32_t> rows = lit_rows(r, b, x);
			// a lit pixel with the handle's outline corners two columns and two rows away is a focused centre
			bool centre_lit = false;
			for (int32_t y : rows) {
				const bool outline_above = r.lit(x - 2, y - 2 + kTop) && r.lit(x + 2, y - 2 + kTop);
				const bool outline_below = r.lit(x - 2, y + 2 + kTop) && r.lit(x + 2, y + 2 + kTop);
				if (outline_above && outline_below) {
					centre_lit = true;
				}
			}
			if (i == 3) {
				expect(centre_lit).to_be_true();
			}
			else {
				expect(centre_lit).to_be_false();
			}
		}
	});

	it("clamps out-of-range values", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		distortion_graph(r.frame, {.saturation = -1.f, .bitcrush = 3.f, .decimation = 9.f, .wavefold = -2.f}, 0);
		expect(inside(r, b, 2)).to_be_true();
	});

	it("matches its goldens", _ {
		const DistortionGraph d{.saturation = 0.4f, .bitcrush = 0.3f, .decimation = 0.3f, .wavefold = 0.5f};
		uitest::Rig48 r48;
		distortion_graph(r48.frame, d, 3);
		expect_golden(self, "ui/widgets/fx_distortion_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		distortion_graph(r64.frame, d, 3);
		expect_golden(self, "ui/widgets/fx_distortion_64.pbm", r64.pbm());
	});
});
describe swing_graph_w("ui::widgets::oled::swing_graph", $ {
	// A row a step's line crosses but no guide dot does (guides dot rows b.top - 1 + 4k).
	const auto off_guide_row = [](const PageBody& b) {
		int32_t y = b.bottom;
		if ((y - (b.top - 1)) % 4 == 0) {
			y--;
		}
		return y;
	};
	// room for the last step pushed a whole pitch late, 2 px wide, inside the body
	const auto pitch_of = [](const PageBody& b) { return (b.right - b.left - 3) / 8; };

	it("draws every step on its straight place at 50", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		swing_graph(r.frame, 50, -1);
		const int32_t x1 = b.left + 2 + pitch_of(b);
		const int32_t y = off_guide_row(b) + kTop;
		expect(r.lit(x1, y)).to_be_true();
		expect(r.lit(x1 + 1, y)).to_be_true();
	});

	it("pushes the odd steps late by the amount", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		swing_graph(r.frame, 75, -1);
		const int32_t p = pitch_of(b);
		const int32_t x1 = b.left + 2 + p;
		const int32_t late = x1 + static_cast<int32_t>(std::lround(0.5f * static_cast<float>(p)));
		const int32_t y = off_guide_row(b) + kTop;
		expect(r.lit(late, y)).to_be_true();
		expect(r.lit(x1, y)).to_be_false();
		expect(r.lit(b.left + 2 + 2 * p, y)).to_be_true();
	});

	it("keeps the last step inside the body at 99", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		swing_graph(r.frame, 99, -1);
		for (int32_t y = 0; y < 43; y++) {
			for (int32_t x = b.right + 1; x < 128; x++) {
				expect(r.lit(x, y + kTop)).to_be_false();
			}
		}
	});

	it("clamps the amount to 1..99", _ {
		uitest::Rig48 hi;
		uitest::Rig48 hi_ref;
		swing_graph(hi.frame, 400, 0);
		swing_graph(hi_ref.frame, 99, 0);
		expect(hi.pbm()).to_equal(hi_ref.pbm());
		uitest::Rig48 lo;
		uitest::Rig48 lo_ref;
		swing_graph(lo.frame, -5, 0);
		swing_graph(lo_ref.frame, 1, 0);
		expect(lo.pbm()).to_equal(lo_ref.pbm());
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		swing_graph(r48.frame, 66, 0);
		expect_golden(self, "ui/widgets/fx_swing_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		swing_graph(r64.frame, 66, 0);
		expect_golden(self, "ui/widgets/fx_swing_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(fx_graphs_spec, duck_graph_w, distortion_graph_w, swing_graph_w)
