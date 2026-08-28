#include "support/frames.h"
#include "ui/oled/frame.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

#include <cstring>

using namespace deluge::ui::oled;
using deluge::gfx::oled::Point;
using deluge::gfx::oled::Rect;
using deluge::gfx::oled::Size;
namespace uitest = deluge::ui::test;

namespace {
constexpr int kW = 128, kH = 64; // uitest::Rig64

// 8x8 arrow, one byte per column, bit 0 = top.
constexpr uint8_t kArrowData[8] = {0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00, 0x00};
constexpr deluge::gfx::oled::Icon kArrow{kArrowData, 8, 8, 1};

int lit_pixels(std::span<const uint8_t> bytes) {
	int n = 0;
	for (uint8_t b : bytes) {
		n += __builtin_popcount(b);
	}
	return n;
}

/// The exact oracle for a region's clip, needing no golden: run \p body through a sub-frame of
/// \p region, run the very same calls through a frame with the same origin but no clip at all,
/// erase everything outside the region there, and require the framebuffers to be byte-identical.
///
/// The unclipped twin is built by aggregate initialisation rather than by `region()` (which would
/// intersect a clip in) -- that is the whole point: `body`'s local coordinates are unchanged, only
/// the bound on where its pixels may land.
template <typename Self, typename Body>
void expect_region_clips(Self& self, Rect region, Body&& body) {
	uitest::Rig64 got;
	uitest::Rig64 want;

	Rect canvas_clip{};
	got.frame.region(region, [&](OledFrame& f) {
		canvas_clip = f.clip;
		body(f);
	});

	const Point origin{want.frame.origin.x + region.x, want.frame.origin.y + region.y};
	OledFrame unclipped{{want.ctx, true},         want.canvas, Rect{0, 0, kW, kH}, origin,
	                    Size{region.w, region.h}, want.style};
	body(unclipped);
	for (int32_t y = 0; y < kH; ++y) {
		for (int32_t x = 0; x < kW; ++x) {
			if (!canvas_clip.contains(Point{x, y})) {
				want.canvas.bytes()[(y >> 3) * kW + x] &= static_cast<uint8_t>(~(1u << (y & 7)));
			}
		}
	}

	expect(lit_pixels(got.canvas.bytes()) > 0).to_be_true(); // not vacuous: something landed inside
	expect(std::memcmp(got.canvas.bytes().data(), want.canvas.bytes().data(), kW * kH / 8) == 0).to_be_true();
}

/// A region deliberately straddling page boundaries (rows 12..30 = pages 1, 2, 3).
constexpr Rect kRegion{20, 12, 24, 19};
} // namespace

// clang-format off
describe oled_frame("ui::oled::OledFrame", $ {
	it("translates by visible_top and clips to the visible area", _ {
		uitest::Rig48 r;
		r.frame.pixel(0, 0);                       // local (0,0) -> canvas (0,5)
		expect(r.lit(0, 5)).to_be_true();
		expect(r.lit(0, 0)).to_be_false();         // the bezel rows stay dark
		r.frame.pixel(0, 43);                      // just past the 43 visible rows: clipped
		expect(r.lit(0, 48 - 1)).to_be_false();    // canvas (0,48) does not exist; (0,47) is the last row
		r.frame.pixel(0, 42);                      // the last visible row DOES draw
		expect(r.lit(0, 47)).to_be_true();
		r.frame.pixel(0, -1);                      // above the frame: clipped, does not reach row 4
		expect(r.lit(0, 4)).to_be_false();
		expect(r.frame.bounds()).to_equal(Rect{0, 0, 128, 43});
	});
	it("regions translate and clip, and nest", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{10, 10, 20, 20}, [&self](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 20, 20});
			f.pixel(0, 0);                          // -> (10,10)
			f.pixel(25, 0);                         // outside: clipped
			f.region(Rect{5, 5, 5, 5}, [&self](OledFrame& g) {
				expect(g.bounds()).to_equal(Rect{0, 0, 5, 5});
				g.pixel(0, 0);                       // -> (15,15)
				g.pixel(6, 0);                       // outside the INNER region, inside the outer: clipped
			});
		});
		expect(r.lit(10, 10)).to_be_true();
		expect(r.lit(35, 10)).to_be_false();
		expect(r.lit(15, 15)).to_be_true();
		expect(r.lit(21, 15)).to_be_false();        // would be lit if the nested clip were the outer one
	});
	it("clips a region that hangs off its parent, and still reports its full local rect", _ {
		uitest::Rig64 r;
		// Bottom-right corner: 20 wide but only 8 of those columns are on the panel.
		r.frame.region(Rect{120, 60, 20, 20}, [&self](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 20, 20});   // spec B section 4: the requested rect
			f.hline(0, 0, 19);                       // asks for 20 columns; 8 exist
			f.vline(0, 0, 19);
		});
		expect(r.lit(127, 60)).to_be_true();
		expect(r.lit(120, 63)).to_be_true();
	});
	it("keeps local (0,0) at the region's corner when it hangs off the left or top", _ {
		uitest::Rig64 r;
		// The case a clipped bounds() would get wrong: reporting {0,0,6,7} here would move the
		// widget's own origin four columns right and three rows down without telling it.
		r.frame.region(Rect{-4, -3, 10, 10}, [&self](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 10, 10});
			f.pixel(0, 0);                           // -> surface (-4,-3): off-panel, clipped
			f.pixel(5, 5);                           // -> surface (1,2): the origin has not moved
		});
		expect(r.lit(1, 2)).to_be_true();
		expect(r.lit(5, 5)).to_be_false();           // where a clipped-bounds origin would have put it
	});
	it("clips spans, outlines and filled areas to the region", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{10, 10, 10, 10}, [](OledFrame& f) {
			f.hline(2, -50, 500);                    // clamped to the region's columns
			f.vline(2, -50, 500);
			f.rect(f.bounds());                      // outline exactly on the region edge
		});
		expect(r.lit(10, 12)).to_be_true();          // hline start
		expect(r.lit(19, 12)).to_be_true();          // hline end
		expect(r.lit(9, 12)).to_be_false();          // ...and no further
		expect(r.lit(20, 12)).to_be_false();
		expect(r.lit(12, 10)).to_be_true();          // vline start
		expect(r.lit(12, 19)).to_be_true();
		expect(r.lit(12, 9)).to_be_false();
		expect(r.lit(12, 20)).to_be_false();
		expect(r.lit(10, 10)).to_be_true();          // rect corners
		expect(r.lit(19, 19)).to_be_true();

		// A filled invert of the whole region flips exactly the region's pixels: the rect's
		// corner goes dark, an untouched interior pixel lights up, and neither neighbouring
		// column outside the region moves.
		r.frame.region(Rect{10, 10, 10, 10}, [](OledFrame& f) { f.invert(f.bounds()); });
		expect(r.lit(10, 10)).to_be_false();
		expect(r.lit(14, 14)).to_be_true();          // interior, dark before the invert
		expect(r.lit(9, 10)).to_be_false();
		expect(r.lit(20, 10)).to_be_false();
	});
	it("clears only within the region", _ {
		uitest::Rig64 r;
		r.frame.hline(5, 0, 127);
		r.frame.region(Rect{40, 0, 10, 10}, [](OledFrame& f) { f.clear(f.bounds()); });
		expect(r.lit(39, 5)).to_be_true();
		expect(r.lit(40, 5)).to_be_false();
		expect(r.lit(49, 5)).to_be_false();
		expect(r.lit(50, 5)).to_be_true();
	});
	it("clips an oversized blit to the region instead of skipping it", _ {
		uitest::Rig64 r;
		// One byte per column, all eight rows set.
		const uint8_t bar[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		r.frame.region(Rect{0, 0, 4, 6}, [&bar](OledFrame& f) {
			f.graphic(bar, 0, 0, 8, 8, 1);           // 8 columns and 8 rows into a 4x6 region
		});
		expect(r.lit(0, 0)).to_be_true();
		expect(r.lit(3, 5)).to_be_true();            // the last column and row that fit
		expect(r.lit(4, 0)).to_be_false();           // ...and not one column further
		expect(r.lit(0, 6)).to_be_false();           // ...nor one row further, mid-page though it is
	});

	// The clip rect reaching every primitive (task 4). Each case is checked against the same
	// drawing done through an unclipped frame at the same origin and then masked to the region --
	// exact, and needing no golden of its own.
	it("clips glyphs half outside the region on each edge", _ {
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyphs("Ag13", -12, 4, 9, 13); }); // left
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyphs("Ag13", 16, 4, 9, 13); });  // right
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyphs("Ag13", 2, -7, 9, 13); });  // top
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyphs("Ag13", 2, 14, 9, 13); });  // bottom
	});

	it("clips a single glyph half outside the region on each edge", _ {
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyph('W', -6, 4, 9, 13); });
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyph('W', 19, 4, 9, 13); });
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyph('W', 6, -6, 9, 13); });
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.glyph('W', 6, 12, 9, 13); });
	});

	it("clips an icon half outside the region", _ {
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.icon(kArrow, -4, 2); });
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.icon(kArrow, 20, 2, true); });
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.icon(kArrow, 4, 15); });
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.icon_centered(kArrow, -20, 40, 2); });
	});

	it("clips a graphic crossing a page boundary", _ {
		expect_region_clips(self, kRegion, [](OledFrame& f) {
			uint8_t graphic[32];
			for (int i = 0; i < 32; i++) {
				graphic[i] = static_cast<uint8_t>(0x81 + i);
			}
			// 16 columns x 16 rows starting above and left of the region: page 1 clipped from
			// above, page 3 clipped from below, page 2 whole.
			f.graphic(graphic, -6, -3, 16, 16, 2);
		});
	});

	it("clips a rounded invert half outside the region", _ {
		expect_region_clips(self, kRegion, [](OledFrame& f) { f.invert_rounded(Rect{-10, -4, 24, 16}); });
		expect_region_clips(self, kRegion, [](OledFrame& f) {
			f.invert_rounded(Rect{10, 8, 24, 16}, deluge::gfx::oled::BorderRadius::Big);
		});
		expect_region_clips(self, kRegion, [](OledFrame& f) {
			f.glyphs("Ag13", 2, 2, 9, 13);           // content first, so the XOR has something to flip
			f.invert_rounded(Rect{-4, 0, 24, 14});
		});
	});

	it("clips a nested region's drawing to the intersection, not to the outer region", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{20, 12, 24, 19}, [](OledFrame& f) {
			f.region(Rect{4, 4, 8, 8}, [](OledFrame& g) {
				g.icon(kArrow, 0, 0);                // 8x8 at inner (0,0) -> canvas (24,16): fits
				g.icon(kArrow, 8, 0);                // -> canvas (32,16): outside the INNER region
			});
		});
		expect(r.lit(24, 16 + 3)).to_be_true();      // column 0 of the arrow, bit 3 of 0x18
		expect(r.lit(31, 16)).to_be_false();         // inner region ends at canvas x 31 inclusive...
		expect(r.lit(32, 16 + 3)).to_be_false();     // ...and the second blit would start there
		expect(r.lit(35, 16 + 3)).to_be_false();     // well inside the OUTER region: still clipped
	});

	it("draws nothing when a clip already on the canvas excludes the region", _ {
		uitest::Rig64 r;
		// The shape of a shell repainting only a damage rectangle: the clip is set on the canvas,
		// outside the frame tree, so no frame knows about it.
		r.canvas.set_clip(Rect{0, 0, 64, 64});       // the left half only
		r.frame.region(Rect{80, 10, 20, 20}, [](OledFrame& f) {
			f.invert(f.bounds());
			f.glyphs("Ag", 0, 0, 9, 13);
			f.icon(kArrow, 0, 0);
			f.rect(f.bounds());
		});
		expect(lit_pixels(r.canvas.bytes())).to_equal(0);  // a region may not widen the canvas clip
		expect(r.canvas.clip().has_value()).to_be_true();  // ...and it is still in force afterwards
		expect(*r.canvas.clip() == Rect{0, 0, 64, 64}).to_be_true();
	});

	it("lets a clip already on the canvas narrow what a region draws", _ {
		uitest::Rig64 bounded;
		bounded.canvas.set_clip(Rect{0, 0, 24, 64});  // cuts the region below in half, at x = 24
		bounded.frame.region(Rect{20, 10, 20, 20}, [](OledFrame& f) { f.invert(f.bounds()); });
		expect(bounded.lit(20, 10)).to_be_true();
		expect(bounded.lit(23, 10)).to_be_true();     // the last column the canvas clip allows
		expect(bounded.lit(24, 10)).to_be_false();    // ...and not one further

		// The same region with no canvas clip does light column 24, so it really was the outer
		// clip that narrowed the draw and not the region's own bounds.
		uitest::Rig64 unbounded;
		unbounded.frame.region(Rect{20, 10, 20, 20}, [](OledFrame& f) { f.invert(f.bounds()); });
		expect(unbounded.lit(24, 10)).to_be_true();
		expect(unbounded.lit(39, 10)).to_be_true();
		expect(unbounded.lit(40, 10)).to_be_false();       // the region's own right edge, either way
	});

	it("ClipScope puts back the clip that was live before it", _ {
		uitest::Rig64 r;
		expect(r.canvas.clip().has_value()).to_be_false();
		{
			ClipScope outer{r.canvas, Rect{0, 0, 40, 40}};
			{
				ClipScope inner{r.canvas, Rect{10, 10, 5, 5}};
				r.canvas.draw_horizontal_line(12, 0, 127);
			}
			expect(r.canvas.clip().has_value()).to_be_true();
			expect(*r.canvas.clip() == Rect{0, 0, 40, 40}).to_be_true();
			// The outer clip, not the inner one, must bound what is drawn after the inner scope
			// closed -- this line lands outside the inner rect entirely.
			r.canvas.draw_horizontal_line(20, 0, 127);
		}
		expect(r.canvas.clip().has_value()).to_be_false();   // and the no-clip state comes back too
		expect(r.lit(10, 12)).to_be_true();                  // the inner line, inside the inner rect
		expect(r.lit(15, 12)).to_be_false();                 // ...and bounded by it
		expect(r.lit(0, 20)).to_be_true();                   // the outer line drew at all
		expect(r.lit(39, 20)).to_be_true();                  // ...across the whole outer rect
		expect(r.lit(40, 20)).to_be_false();                 // ...and no further
	});

	it("keeps drawing in the outer region once a nested one is done", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{20, 12, 24, 19}, [](OledFrame& f) {
			f.region(Rect{0, 0, 4, 4}, [](OledFrame& g) { g.invert(g.bounds()); });
			// Outside the inner rect but inside the outer one: this must still land, which it
			// only does if the inner ClipScope put the outer clip back.
			f.glyphs("Ag", 6, 2, 9, 13);
			f.icon(kArrow, 14, 8);
		});
		expect(r.lit(20, 12)).to_be_true();          // the inner invert
		expect(r.lit(24, 12)).to_be_false();         // ...which stopped at the inner region's edge
		expect(lit_pixels(r.canvas.bytes()) > 16).to_be_true(); // the text and icon after it drew
		expect(r.lit(34 + 3, 20 + 3)).to_be_true();  // icon column 3 (0x7E), row 20+3
	});

	it("puts the root frame's clip back after a region", _ {
		uitest::Rig48 r;                             // 128x48 with a five-row bezel
		r.frame.region(Rect{10, 10, 8, 8}, [](OledFrame& f) { f.invert(f.bounds()); });
		r.frame.pixel(0, -1);                        // still above the visible area: still clipped
		expect(r.lit(0, 4)).to_be_false();
		r.frame.pixel(0, 0);                         // and the root's own region still draws
		expect(r.lit(0, 5)).to_be_true();
		expect(r.canvas.clip().has_value()).to_be_false(); // nothing left a clip behind on the canvas
	});
});
DELUGE_SPEC(ui_oled_frame_spec, oled_frame)
