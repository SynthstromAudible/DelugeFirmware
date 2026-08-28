#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gfx/grid/tween.h"
#include "support/golden.h"
#include "support/grid_state.h"
#include "tween_oracle.h"
#include <array>

using namespace deluge::gfx::grid;

namespace {

/// Three colours so a blend that mixes the wrong pair is visible in every channel, laid out with
/// black gaps so the "is this source pad lit" test the ports substitute for legacy's occupancy
/// mask actually gets exercised. 18x8 = the legacy `image` shape the oracles transcribe against.
constexpr RGB kPalette[3] = {RGB{200, 40, 40}, RGB{40, 200, 60}, RGB{50, 60, 220}};

PadGrid make_test_grid(uint32_t seed) {
	PadGrid g(kDelugeGrid);
	for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
		for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
			const uint32_t h = x * 7u + y * 13u + seed * 3u;
			if (h % 5u == 0u) {
				continue; // black
			}
			g.set(x, y, kPalette[h % 3u]);
		}
	}
	return g;
}

void grid_to_image(const PadGrid& g, RGB image[legacy::kDisplayHeight][legacy::kTotalWidth]) {
	for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
		for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
			image[y][x] = g.at(x, y);
		}
	}
}

PadGrid image_to_grid(const RGB image[legacy::kDisplayHeight][legacy::kTotalWidth]) {
	PadGrid g(kDelugeGrid);
	for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
		for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
			g.set(x, y, image[y][x]);
		}
	}
	return g;
}

/// Occupancy in the port's `OccupancyMask` layout: one word per column, bit y = row.
using MaskWords = std::array<uint32_t, legacy::kTotalWidth>;

/// "Non-black is occupied" as a mask -- what an empty `OccupancyMask` means, made explicit so the
/// oracle (which always takes a real mask) can be fed the same rule the port's default applies.
MaskWords occupancy_from_colour(const PadGrid& g) {
	MaskWords m{};
	for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
		for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
			const RGB c = g.at(x, y);
			if (c.r | c.g | c.b) {
				m[x] |= 1u << y;
			}
		}
	}
	return m;
}

/// The same mask in legacy's `occupancyMaskStore[y][x]` shape (64 = fully occupied).
void mask_to_store(const MaskWords& m, uint8_t store[legacy::kDisplayHeight][legacy::kTotalWidth]) {
	for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
		for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
			store[y][x] = ((m[x] >> y) & 1u) ? 64 : 0;
		}
	}
}

} // namespace

// clang-format off
describe tween("gfx::grid tweens", $ {
	it("fade hits both endpoints exactly", _ {
		PadGrid a(kDelugeGrid), b(kDelugeGrid), out(kDelugeGrid);
		a.set(2, 2, RGB{200, 0, 0});
		b.set(2, 2, RGB{0, 0, 200});
		fade(a, b, 0, out);
		expect(out.at(2, 2)).to_equal(RGB{200, 0, 0});
		fade(a, b, 65535, out);
		expect(out.at(2, 2)).to_equal(RGB{0, 0, 200});
		expect(out.dirty_rows()).to_equal(0xFFu);
	});

	it("scroll_horizontal moves main pads and pulls incoming columns in, leaving the sidebar alone", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(0, 0, RGB{9, 9, 9});
		a.set(16, 0, RGB{7, 7, 7});      // sidebar
		in.set(15, 0, RGB{1, 2, 3});     // rightmost incoming main column
		scroll_horizontal(a, in, 1, out); // content moves right by one
		expect(out.at(1, 0)).to_equal(RGB{9, 9, 9});
		expect(out.at(0, 0)).to_equal(RGB{1, 2, 3});
		expect(out.at(16, 0)).to_equal(RGB{7, 7, 7});
		in.set(0, 0, RGB{4, 4, 4});
		scroll_horizontal(a, in, -1, out); // content moves left by one; column 15 comes from incoming's column 0
		expect(out.at(15, 0)).to_equal(RGB{4, 4, 4});
		expect(out.at(0, 0)).to_equal(RGB{0, 0, 0});
	});

	it("scroll_vertical moves rows", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(3, 0, RGB{5, 5, 5});
		in.set(3, 7, RGB{6, 6, 6});
		scroll_vertical(a, in, 1, out);
		expect(out.at(3, 1)).to_equal(RGB{5, 5, 5});
		expect(out.at(3, 0)).to_equal(RGB{6, 6, 6});
	});

	it("clamps scroll_horizontal's offset to the main-axis extent", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out16(kDelugeGrid), out17(kDelugeGrid);
		a.set(0, 0, RGB{9, 9, 9});
		a.set(16, 0, RGB{7, 7, 7}); // sidebar
		in.set(5, 3, RGB{1, 2, 3});
		// offset = 17 has no more meaning than offset = 16 (the full main-axis extent): both must
		// clamp to the same all-incoming-on-main-columns result, without reading out of range.
		scroll_horizontal(a, in, 16, out16);
		scroll_horizontal(a, in, 17, out17);
		expect(grid_to_state(out17)).to_equal(grid_to_state(out16));
		for (uint8_t x = 0; x < kDelugeGrid.main_cols; x++) {
			expect(out17.at(x, 3)).to_equal(in.at(x, 3));
		}
		expect(out17.at(16, 0)).to_equal(RGB{7, 7, 7}); // sidebar still comes from `a`, untouched
	});

	it("clamps scroll_vertical's offset to the row extent", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out8(kDelugeGrid), out9(kDelugeGrid);
		a.set(1, 0, RGB{9, 9, 9});
		in.set(4, 2, RGB{8, 8, 8});
		// offset = -9 has no more meaning than offset = -8 (the full row extent): both must clamp
		// to the same all-incoming result (sidebar columns included), without reading out of range.
		scroll_vertical(a, in, -8, out8);
		scroll_vertical(a, in, -9, out9);
		expect(grid_to_state(out9)).to_equal(grid_to_state(out8));
		for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
			expect(out9.at(x, 0)).to_equal(in.at(x, 0));
		}
	});

	it("grey_out dims main pads only", _ {
		PadGrid a(kDelugeGrid), out(kDelugeGrid);
		a.set(0, 0, RGB{255, 255, 255});
		a.set(17, 0, RGB{255, 255, 255});
		// RGB::greyOut's proportion is on a ~0..6500000 scale (see pad_leds.cpp/session_view.cpp/
		// load_song_ui.cpp callers), not the 0..65535 scale RGB::blend's index uses -- 65536/2 leaves
		// a white pixel completely unchanged under the real formula, so use a value on the real scale.
		grey_out(a, 6500000 / 2, out);
		expect(out.at(0, 0).r).to_be_less_than(uint8_t(255));
		expect(out.at(17, 0)).to_equal(RGB{255, 255, 255});
	});

	// --- smear scroll -------------------------------------------------------------------------
	// No legacy software equivalent to compare against (horizontal::renderScroll hands the
	// intermediate positions to the PIC), so: one hand-computed half-pad blend, and the property
	// that ties the port back to the legacy behaviour that *does* exist -- a whole-pad offset must
	// reproduce the whole-square scroll exactly.

	it("smear_scroll_horizontal splits a pad evenly across two columns at a half-pad offset", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(5, 0, RGB{200, 100, 50});
		smear_scroll_horizontal(a, in, 32768, out); // half a pad to the right
		// col 5 samples [4, 5] at frac 0.5 -> half of {200,100,50}; col 6 samples [5, 6] -> the
		// other half. RGB::blendChannel2 rounds: (200*32768 + 32768) >> 16 == 100, 100 -> 50,
		// 50 -> 25.
		expect(out.at(5, 0)).to_equal(RGB{100, 50, 25});
		expect(out.at(6, 0)).to_equal(RGB{100, 50, 25});
		expect(out.at(4, 0)).to_equal(RGB{0, 0, 0});
		expect(out.at(7, 0)).to_equal(RGB{0, 0, 0});
		expect(out.dirty_rows()).to_equal(0xFFu);
	});

	it("smear_scroll_vertical splits a pad evenly across two rows at a half-pad offset", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(3, 2, RGB{200, 100, 50});
		smear_scroll_vertical(a, in, 32768, out); // half a pad down
		expect(out.at(3, 2)).to_equal(RGB{100, 50, 25});
		expect(out.at(3, 3)).to_equal(RGB{100, 50, 25});
		expect(out.at(3, 1)).to_equal(RGB{0, 0, 0});
	});

	it("smear scroll at a whole-pad offset is exactly the whole-square scroll", _ {
		PadGrid a = make_test_grid(11), in = make_test_grid(12);
		for (int32_t pads = -3; pads <= 3; pads++) {
			PadGrid smeared(kDelugeGrid), stepped(kDelugeGrid);
			smear_scroll_horizontal(a, in, pads * 65536, smeared);
			scroll_horizontal(a, in, pads, stepped);
			expect(grid_to_state(smeared)).to_equal(grid_to_state(stepped));
			smear_scroll_vertical(a, in, pads * 65536, smeared);
			scroll_vertical(a, in, pads, stepped);
			expect(grid_to_state(smeared)).to_equal(grid_to_state(stepped));
		}
	});

	it("smear scroll clamps to the full extent, reading black past the wrapped edge", _ {
		PadGrid a = make_test_grid(13), in = make_test_grid(14);
		PadGrid at_extent(kDelugeGrid), past_extent(kDelugeGrid);
		smear_scroll_horizontal(a, in, 16 * 65536, at_extent);
		smear_scroll_horizontal(a, in, 40 * 65536, past_extent);
		expect(grid_to_state(past_extent)).to_equal(grid_to_state(at_extent));
		smear_scroll_vertical(a, in, -8 * 65536, at_extent);
		smear_scroll_vertical(a, in, -40 * 65536, past_extent);
		expect(grid_to_state(past_extent)).to_equal(grid_to_state(at_extent));
	});

	// --- zoom ---------------------------------------------------------------------------------

	// A sanity anchor for the oracle byte-compares below: they would agree just as happily on two
	// identically-wrong transcriptions, so pin the one thing the animation must obviously do.
	it("zoom starts on `from` at native scale", _ {
		PadGrid from = make_test_grid(3), to = make_test_grid(4), out(kDelugeGrid);
		zoom(from, to, ZoomParams{6, 3, 9 * 65536, 9 * 65536, 0}, out);
		for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
			for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
				expect(out.at(x, y)).to_equal(from.at(x, y));
			}
		}
	});

	it("expand_collapse starts fully expanded on `from`, over `to` where `from` is dark", _ {
		PadGrid from = make_test_grid(3), to = make_test_grid(4), out(kDelugeGrid);
		expand_collapse(from, to, ExpandCollapseParams{0, 5, true, 0}, out);
		for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
			for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
				const RGB f = from.at(x, y);
				expect(out.at(x, y)).to_equal((f.r | f.g | f.b) ? f : to.at(x, y));
			}
		}
	});

	it("explode is fully imploded at t = 0 and spreads as it runs", _ {
		PadGrid from = make_test_grid(3), out(kDelugeGrid);
		auto lit_count = [&out] {
			int32_t n = 0;
			for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
				for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
					const RGB c = out.at(x, y);
					n += ((c.r | c.g | c.b) != 0) ? 1 : 0;
				}
			}
			return n;
		};
		explode(from, ExplodeParams{3 * 65536, 4 * 65536, 5 * 65536, false, 0}, out);
		const int32_t at_rest = lit_count();
		explode(from, ExplodeParams{3 * 65536, 4 * 65536, 5 * 65536, false, 32768}, out);
		const int32_t half_way = lit_count();
		explode(from, ExplodeParams{3 * 65536, 4 * 65536, 5 * 65536, false, 65535}, out);
		const int32_t spread = lit_count();
		expect(at_rest).to_be_less_than(half_way);
		expect(half_way).to_be_less_than(spread);
	});

	it("zoom reproduces renderZoom byte for byte, zooming in and out", _ {
		PadGrid from = make_test_grid(3), to = make_test_grid(4);
		RGB inner_image[8][18], outer_image[8][18];
		const int32_t pin = 9 * 65536;

		// {old_zoom, new_zoom, the legacy zoomingIn / zoomMagnitude they stand for}
		struct Case {
			int32_t old_zoom;
			int32_t new_zoom;
			bool zooming_in;
			int8_t magnitude;
		};
		const Case cases[] = {{6, 3, true, 1}, {3, 6, false, 1}, {12, 3, true, 2}, {3, 12, false, 2}};

		for (const Case& c : cases) {
			for (int32_t t : {0, 32768, 65535}) {
				PadGrid out(kDelugeGrid);
				zoom(from, to, ZoomParams{c.old_zoom, c.new_zoom, pin, pin, static_cast<uint16_t>(t)}, out);

				// timeline_view.cpp:316-325: the more-zoomed-in frame is the inner one.
				grid_to_image(c.zooming_in ? to : from, inner_image);
				grid_to_image(c.zooming_in ? from : to, outer_image);
				RGB image[8][18];
				// Legacy never clears `image`; renderZoomWithProgress writes only the main
				// columns, so the sidebar holds the previously rendered frame -- `from`'s.
				grid_to_image(from, image);
				legacy::renderZoom(inner_image, outer_image, c.zooming_in, c.magnitude, pin, t, image);

				expect(grid_to_state(out)).to_equal(grid_to_state(image_to_grid(image)));
			}
		}
	});

	// The oracle compares above all pass `pin_square_from == pin_square_to`, which is the case
	// legacy's two `#define`s (:1238-1239) alias to one array -- so the one thing this port adds
	// over legacy, three independently addressable pin roles, would otherwise have no coverage.
	it("zoom's two pin squares are independent, and agreeing is the legacy case", _ {
		PadGrid from = make_test_grid(3), to = make_test_grid(4);
		PadGrid aliased(kDelugeGrid), split(kDelugeGrid), also_aliased(kDelugeGrid);
		zoom(from, to, ZoomParams{6, 3, 9 * 65536, 9 * 65536, 32768}, aliased);
		zoom(from, to, ZoomParams{6, 3, 9 * 65536, 5 * 65536, 32768}, split);
		zoom(from, to, ZoomParams{6, 3, 9 * 65536, 9 * 65536, 32768}, also_aliased);
		// Pinning the incoming image at a different square really does move it...
		expect(grid_to_state(split) == grid_to_state(aliased)).to_be_false();
		// ...and the aliased call is deterministic, i.e. the difference is the pin, not noise.
		expect(grid_to_state(also_aliased)).to_equal(grid_to_state(aliased));
	});

	// I3: the largest magnitude the arithmetic survives. `coarse` reaches -2^(magnitude - 1) at
	// t == 0, so increase_magnitude shifts right by 2^(magnitude - 1) + 14 -- 30 here, 46 at
	// magnitude 6. The trap probe for the far side lives in contract_spec.cpp.
	it("zoom works at the largest legal magnitude", _ {
		PadGrid from = make_test_grid(3), to = make_test_grid(4), out(kDelugeGrid);
		RGB inner_image[8][18], outer_image[8][18], image[8][18];
		const int32_t pin = 9 * 65536;
		// bit_width(3) == 2, bit_width(96) == 7 -> magnitude 5 == kMaxZoomMagnitude.
		for (int32_t t : {0, 32768, 65535}) {
			zoom(from, to, ZoomParams{96, 3, pin, pin, static_cast<uint16_t>(t)}, out);
			grid_to_image(to, inner_image); // 3 < 96, so this is a zoom in
			grid_to_image(from, outer_image);
			grid_to_image(from, image);
			legacy::renderZoom(inner_image, outer_image, true, kMaxZoomMagnitude, pin, t, image);
			expect(grid_to_state(out)).to_equal(grid_to_state(image_to_grid(image)));
		}
	});

	// --- explode ------------------------------------------------------------------------------

	it("explode reproduces renderExplodeAnimation byte for byte, exploding and imploding", _ {
		PadGrid src = make_test_grid(1);
		const int32_t origin_x_big = 3 * 65536;
		const int32_t origin_y_big = 4 * 65536;
		const int32_t width_big = 5 * 65536;

		RGB store[10][18] = {};        // legacy's imageStore: row 0 / row 9 are the off-screen margin
		uint8_t occ_store[10][18] = {}; // and its occupancyMaskStore
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 18; x++) {
				store[y + 1][x] = src.at(x, y);
				const RGB c = src.at(x, y);
				occ_store[y + 1][x] = (c.r | c.g | c.b) ? 64 : 0; // the port's default rule
			}
		}

		for (bool implode : {false, true}) {
			for (int32_t t : {0, 32768, 65535}) {
				PadGrid out(kDelugeGrid);
				explode(src, ExplodeParams{origin_x_big, origin_y_big, width_big, implode, static_cast<uint16_t>(t)},
				        out);

				// pad_leds.cpp:956-957.
				const int32_t explodedness = implode ? (65536 - t) : t;
				RGB image[8][18];
				legacy::renderExplodeAnimation(store, occ_store, explodedness, origin_x_big, width_big, origin_y_big,
				                               image);

				expect(grid_to_state(out)).to_equal(grid_to_state(image_to_grid(image)));
			}
		}
	});

	// --- expand / collapse --------------------------------------------------------------------

	it("expand_collapse reproduces the renderInstrumentClipCollapseAnimation core byte for byte", _ {
		PadGrid src = make_test_grid(2), dest = make_test_grid(7);
		RGB store[8][18];
		grid_to_image(src, store);
		uint8_t occ_store[8][18], dest_occ[8][18];
		mask_to_store(occupancy_from_colour(src), occ_store); // the port's default rule, made explicit
		mask_to_store(occupancy_from_colour(dest), dest_occ);

		for (uint8_t row_to : {0, 5, 7}) {
			for (bool collapsing : {false, true}) {
				for (int32_t t : {0, 32768, 65535}) {
					PadGrid out(kDelugeGrid);
					expand_collapse(src, dest, ExpandCollapseParams{0, row_to, collapsing, static_cast<uint16_t>(t)},
					                out);

					int16_t going_from[8], going_to[8];
					for (int32_t i = 0; i < 8; i++) {
						going_from[i] = static_cast<int16_t>(i); // row_from == 0
						going_to[i] = row_to;
					}
					// Legacy's `progress` is expandedness, so each caller flips it for a collapse
					// (pad_leds.cpp:1096, :1163).
					const int32_t progress = collapsing ? (65536 - t) : t;
					RGB image[8][18];
					grid_to_image(dest, image);
					legacy::renderInstrumentClipCollapseAnimation(store, occ_store, dest_occ, going_from, going_to, 8,
					                                             progress, image);

					expect(grid_to_state(out)).to_equal(grid_to_state(image_to_grid(image)));
				}
			}
		}
	});

	// I1: the third legacy caller. `renderNoteRowExpandOrCollapse` sets `animatedRowGoingTo[i]`
	// per note row (`instrument_clip_view.cpp:5781`), which the scalar `row_to` cannot express.
	it("expand_collapse follows per-row destinations, as renderNoteRowExpandOrCollapse needs", _ {
		PadGrid src = make_test_grid(2), dest = make_test_grid(7);
		RGB store[8][18];
		grid_to_image(src, store);
		uint8_t occ_store[8][18], dest_occ[8][18];
		mask_to_store(occupancy_from_colour(src), occ_store);
		mask_to_store(occupancy_from_colour(dest), dest_occ);

		// A scale-mode shuffle: every row goes somewhere different, two of them stay put.
		const std::array<uint8_t, 8> destinations{3, 3, 4, 4, 4, 5, 6, 6};

		for (int32_t t : {0, 32768, 65535}) {
			PadGrid out(kDelugeGrid);
			expand_collapse(src, dest,
			                ExpandCollapseParams{.row_from = 0,
			                                     .row_to = 0, // ignored while row_to_by_row is non-empty
			                                     .collapsing = true,
			                                     .t = static_cast<uint16_t>(t),
			                                     .row_to_by_row = destinations},
			                out);

			int16_t going_from[8], going_to[8];
			for (int32_t i = 0; i < 8; i++) {
				going_from[i] = static_cast<int16_t>(i);
				going_to[i] = destinations[static_cast<size_t>(i)];
			}
			RGB image[8][18];
			grid_to_image(dest, image);
			legacy::renderInstrumentClipCollapseAnimation(store, occ_store, dest_occ, going_from, going_to, 8,
			                                             65536 - t, image);

			expect(grid_to_state(out)).to_equal(grid_to_state(image_to_grid(image)));
		}

		// And it really is different from what the scalar could produce -- otherwise the compare
		// above would pass just as happily with row_to_by_row ignored.
		PadGrid per_row(kDelugeGrid), scalar(kDelugeGrid);
		expand_collapse(src, dest,
		                ExpandCollapseParams{
		                    .row_from = 0, .row_to = 3, .collapsing = true, .t = 32768, .row_to_by_row = destinations},
		                per_row);
		expand_collapse(src, dest, ExpandCollapseParams{.row_from = 0, .row_to = 3, .collapsing = true, .t = 32768},
		                scalar);
		expect(grid_to_state(per_row) == grid_to_state(scalar)).to_be_false();
	});

	// I2: `View::renderMacros` (view.cpp:2805-2828) paints an armed clip's macro pad black on the
	// flash-off frame and sets its occupancy anyway, and that column reaches `occupancyMaskStore`
	// through both animation paths ported here. Legacy blends that black in; the "non-black is
	// occupied" default cannot, which is exactly why the mask is an input.
	it("expand_collapse honours a black-but-occupied source pad, which the colour rule misses", _ {
		PadGrid src = make_test_grid(2), dest(kDelugeGrid);
		// A bright destination row for the black pad to be blended into, so "black was drawn here"
		// and "nothing was drawn here" cannot look the same.
		for (uint8_t x = 0; x < kDelugeGrid.cols(); x++) {
			for (uint8_t y = 0; y < kDelugeGrid.rows; y++) {
				dest.set(x, y, RGB{255, 255, 255});
			}
		}
		const uint8_t kMacroCol = 16; // the sidebar column renderMacros writes
		const uint8_t kMacroRow = 2;
		src.set(kMacroCol, kMacroRow, RGB{0, 0, 0}); // armed clip, flash-off frame

		MaskWords from_mask = occupancy_from_colour(src);
		from_mask[kMacroCol] |= 1u << kMacroRow; // ...but occupied, as renderMacros marks it
		const MaskWords dest_mask = occupancy_from_colour(dest);

		const ExpandCollapseParams with_mask{.row_from = 0,
		                                     .row_to = 4,
		                                     .collapsing = true,
		                                     .t = 32768,
		                                     .from_occupancy = from_mask,
		                                     .to_occupancy = dest_mask};
		PadGrid masked(kDelugeGrid), unmasked(kDelugeGrid);
		expand_collapse(src, dest, with_mask, masked);
		expand_collapse(src, dest, ExpandCollapseParams{.row_from = 0, .row_to = 4, .collapsing = true, .t = 32768},
		                unmasked);

		// The mask must change the result: the black pad now darkens the two rows it straddles.
		expect(grid_to_state(masked) == grid_to_state(unmasked)).to_be_false();

		// And it must change it the way legacy would, byte for byte.
		RGB store[8][18];
		grid_to_image(src, store);
		uint8_t occ_store[8][18], dest_occ[8][18];
		mask_to_store(from_mask, occ_store);
		mask_to_store(dest_mask, dest_occ);
		int16_t going_from[8], going_to[8];
		for (int32_t i = 0; i < 8; i++) {
			going_from[i] = static_cast<int16_t>(i);
			going_to[i] = 4;
		}
		RGB image[8][18];
		grid_to_image(dest, image);
		legacy::renderInstrumentClipCollapseAnimation(store, occ_store, dest_occ, going_from, going_to, 8, 65536 - 32768,
		                                             image);
		expect(grid_to_state(masked)).to_equal(grid_to_state(image_to_grid(image)));
	});

	it("explode honours a black-but-occupied source pad too", _ {
		PadGrid src = make_test_grid(1);
		const uint8_t kMacroCol = 4;
		const uint8_t kMacroRow = 2;
		src.set(kMacroCol, kMacroRow, RGB{0, 0, 0});

		MaskWords from_mask = occupancy_from_colour(src);
		from_mask[kMacroCol] |= 1u << kMacroRow;

		const int32_t ox = 3 * 65536, oy = 4 * 65536, w = 5 * 65536;
		PadGrid masked(kDelugeGrid), unmasked(kDelugeGrid);
		explode(src, ExplodeParams{ox, oy, w, false, 32768, from_mask}, masked);
		explode(src, ExplodeParams{ox, oy, w, false, 32768}, unmasked);

		// `explode` draws onto a cleared grid, so blending black in changes the *occupancy* the
		// later source pads are marginalised against, not the first colour written. Byte-compare
		// against the oracle is the real assertion; that it differs at all is the cheap one.
		RGB store[10][18] = {};
		uint8_t occ_store[10][18] = {};
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 18; x++) {
				store[y + 1][x] = src.at(x, y);
				occ_store[y + 1][x] = ((from_mask[x] >> y) & 1u) ? 64 : 0;
			}
		}
		RGB image[8][18];
		legacy::renderExplodeAnimation(store, occ_store, 32768, ox, w, oy, image);
		expect(grid_to_state(masked)).to_equal(grid_to_state(image_to_grid(image)));
		expect(grid_to_state(masked) == grid_to_state(unmasked)).to_be_false();
	});

	// --- goldens ------------------------------------------------------------------------------
	// Recorded only after the oracles above passed, so they freeze legacy-parity output rather
	// than whatever the port happened to produce.

	it("freezes a mid-transition frame of each new tween", _ {
		PadGrid a = make_test_grid(1), b = make_test_grid(7), out(kDelugeGrid);

		smear_scroll_horizontal(a, b, 3 * 65536 + 32768, out);
		expect_golden(self, "grid/tween_smear_scroll_horizontal.state", grid_to_state(out));
		smear_scroll_vertical(a, b, 2 * 65536 + 16384, out);
		expect_golden(self, "grid/tween_smear_scroll_vertical.state", grid_to_state(out));

		zoom(a, b, ZoomParams{6, 3, 9 * 65536, 9 * 65536, 32768}, out);
		expect_golden(self, "grid/tween_zoom_in.state", grid_to_state(out));
		zoom(a, b, ZoomParams{3, 6, 9 * 65536, 9 * 65536, 32768}, out);
		expect_golden(self, "grid/tween_zoom_out.state", grid_to_state(out));

		explode(a, ExplodeParams{3 * 65536, 4 * 65536, 5 * 65536, false, 32768}, out);
		expect_golden(self, "grid/tween_explode.state", grid_to_state(out));
		// A different `t` from the explode above: at t == 32768 the implode branch computes the
		// same explodedness, so the two frames would be identical.
		explode(a, ExplodeParams{3 * 65536, 4 * 65536, 5 * 65536, true, 16384}, out);
		expect_golden(self, "grid/tween_implode.state", grid_to_state(out));

		expand_collapse(a, b, ExpandCollapseParams{0, 5, true, 32768}, out);
		expect_golden(self, "grid/tween_collapse.state", grid_to_state(out));
		// Likewise: `collapsing` only flips `t`, so a mid-point `t` renders the same frame either
		// way -- take the expand golden a quarter of the way through instead.
		expand_collapse(a, b, ExpandCollapseParams{0, 5, false, 16384}, out);
		expect_golden(self, "grid/tween_expand.state", grid_to_state(out));
	});
});

DELUGE_SPEC(gfx_tween_spec, tween)
