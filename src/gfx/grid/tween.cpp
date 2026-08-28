#include "gfx/grid/tween.h"
#include "gfx/contract.h"
#include "gfx/grid/exp_table.h"
#include "gfx/grid/sine_table.h"
#include <algorithm>
#include <bit>
#include <cstring>

namespace deluge::gfx::grid {

namespace {

bool is_lit(RGB c) {
	return (c.r | c.g | c.b) != 0;
}

/// Legacy's `if (occupancyMaskStore[y][x])` test, against an explicit mask when the caller has one
/// and against the pad's own colour otherwise. See `OccupancyMask` in tween.h for why both exist.
bool occupied(OccupancyMask mask, const PadGrid& g, uint8_t x, uint8_t y) {
	if (!mask.empty()) {
		return (mask[x] & (1u << y)) != 0;
	}
	return is_lit(g.at(x, y));
}

/// Port of legacy `drawSquare`, which every animation that accumulates several source pads into
/// one destination pad goes through.
///
/// Legacy's fifth parameter, `occupancyFromWhichColourCame`, is dropped: legacy's own use of it is
/// dead (it unconditionally leaves the intensity unchanged), so it has no effect on the result.
/// The occupancy the callers *do* need is the running destination mask, which stays.
RGB draw_square(RGB square_colour, int32_t intensity, RGB square, uint8_t* occupancy_mask) {
	int32_t modified_intensity = intensity;
	int32_t colour_remaining_amount = 65536;
	int32_t max_old_occupancy = (65536 - modified_intensity) >> 10;
	if (*occupancy_mask > max_old_occupancy) {
		colour_remaining_amount = (max_old_occupancy << 16) / *occupancy_mask;
	}
	int32_t new_occupancy_mask_value =
	    static_cast<int32_t>(detail::rshift_round(
	        static_cast<uint32_t>(*occupancy_mask) * static_cast<uint32_t>(colour_remaining_amount), 16))
	    + static_cast<int32_t>(detail::rshift_round(static_cast<uint32_t>(modified_intensity), 10));
	*occupancy_mask = static_cast<uint8_t>(std::min<int32_t>(64, new_occupancy_mask_value));
	return RGB::blend2(square, square_colour, static_cast<uint32_t>(colour_remaining_amount),
	                   static_cast<uint32_t>(modified_intensity));
}

/// Port of the SDK's `get_source_color`, restricted to the main columns of a `PadGrid` pair.
RGB smear_sample_col(const PadGrid& from, const PadGrid& incoming, int32_t x, uint8_t y, int32_t main) {
	if (x >= 0 && x < main) {
		return from.at(static_cast<uint8_t>(x), y);
	}
	const int32_t wrapped = (x < 0) ? (main + x) : (x - main);
	if (wrapped >= 0 && wrapped < main) {
		return incoming.at(static_cast<uint8_t>(wrapped), y);
	}
	return RGB{};
}

RGB smear_sample_row(const PadGrid& from, const PadGrid& incoming, uint8_t x, int32_t y, int32_t rows) {
	if (y >= 0 && y < rows) {
		return from.at(x, static_cast<uint8_t>(y));
	}
	const int32_t wrapped = (y < 0) ? (rows + y) : (y - rows);
	if (wrapped >= 0 && wrapped < rows) {
		return incoming.at(x, static_cast<uint8_t>(wrapped));
	}
	return RGB{};
}

} // namespace

void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out) {
	const auto d = from.dims();
	// tween.h's documented precondition, now enforced: with mismatched dims the loops below would
	// index `out` (or `to`) past its own extent. Unconditional -- see gfx/contract.h.
	DELUGE_GFX_CONTRACT(to.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			// RGB::blend(a, b, index): index 0 = b, 65535 = a.
			out.at(x, y) = RGB::blend(to.at(x, y), from.at(x, y), t);
		}
	}
	out.mark_all_dirty();
}

void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(incoming.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	const int32_t main = d.main_cols;
	// A scroll by the full main-axis extent already yields an all-`incoming` result; anything
	// larger has no further meaning and would index PadGrid::at's uint8_t params out of range.
	offset = std::clamp<int32_t>(offset, -main, main);
	for (uint8_t y = 0; y < d.rows; y++) {
		// uint8_t loop variables: PadGrid::at takes uint8_t, and main_cols/cols() are uint8_t-ranged
		// by construction, so counting in int32_t only bought a narrowing conversion at every call.
		// The source index still needs int32_t -- `x - offset` is legitimately negative or >= main.
		for (uint8_t x = 0; x < d.main_cols; x++) {
			const int32_t src = static_cast<int32_t>(x) - offset;
			if (src >= 0 && src < main) {
				out.at(x, y) = from.at(static_cast<uint8_t>(src), y);
			}
			else if (offset > 0) {
				out.at(x, y) = incoming.at(static_cast<uint8_t>(main + src), y); // src < 0: incoming's right edge
			}
			else {
				out.at(x, y) = incoming.at(static_cast<uint8_t>(src - main), y); // src >= main: incoming's left edge
			}
		}
		for (uint8_t x = d.main_cols; x < d.cols(); x++) {
			out.at(x, y) = from.at(x, y);
		}
	}
	out.mark_all_dirty();
}

void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(incoming.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	const int32_t rows = d.rows;
	// Same reasoning as scroll_horizontal's clamp: a full-extent scroll is already all-`incoming`.
	offset = std::clamp<int32_t>(offset, -rows, rows);
	for (uint8_t y = 0; y < d.rows; y++) {
		const int32_t src = static_cast<int32_t>(y) - offset;
		for (uint8_t x = 0; x < d.cols(); x++) {
			if (src >= 0 && src < rows) {
				out.at(x, y) = from.at(x, static_cast<uint8_t>(src));
			}
			else if (offset > 0) {
				out.at(x, y) = incoming.at(x, static_cast<uint8_t>(rows + src));
			}
			else {
				out.at(x, y) = incoming.at(x, static_cast<uint8_t>(src - rows));
			}
		}
	}
	out.mark_all_dirty();
}

void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out) {
	const auto d = in.dims();
	DELUGE_GFX_CONTRACT(out.dims() == d);
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			RGB c = in.at(x, y);
			out.at(x, y) = (x < d.main_cols) ? c.greyOut(proportion) : c;
		}
	}
	out.mark_all_dirty();
}

void smear_scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(incoming.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	const int32_t main = d.main_cols;
	offset_px_16 = std::clamp<int32_t>(offset_px_16, -main * 65536, main * 65536);
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.main_cols; x++) {
			// Arithmetic shift, i.e. floor -- matches the SDK's floor semantics for the source position.
			const int32_t source_16 = (static_cast<int32_t>(x) << 16) - offset_px_16;
			const int32_t source = source_16 >> 16;
			const uint16_t frac = static_cast<uint16_t>(source_16 & 0xFFFF);
			const RGB here = smear_sample_col(from, incoming, source, y, main);
			const RGB next = smear_sample_col(from, incoming, source + 1, y, main);
			// RGB::blend's index weights its *first* argument, so `next` goes first: frac == 0
			// must return `here` (see the SDK deviation noted in tween.h).
			out.at(x, y) = RGB::blend(next, here, frac);
		}
		for (uint8_t x = d.main_cols; x < d.cols(); x++) {
			out.at(x, y) = from.at(x, y);
		}
	}
	out.mark_all_dirty();
}

void smear_scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(incoming.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	const int32_t rows = d.rows;
	offset_px_16 = std::clamp<int32_t>(offset_px_16, -rows * 65536, rows * 65536);
	for (uint8_t y = 0; y < d.rows; y++) {
		const int32_t source_16 = (static_cast<int32_t>(y) << 16) - offset_px_16;
		const int32_t source = source_16 >> 16;
		const uint16_t frac = static_cast<uint16_t>(source_16 & 0xFFFF);
		for (uint8_t x = 0; x < d.cols(); x++) {
			const RGB here = smear_sample_row(from, incoming, x, source, rows);
			const RGB next = smear_sample_row(from, incoming, x, source + 1, rows);
			out.at(x, y) = RGB::blend(next, here, frac);
		}
	}
	out.mark_all_dirty();
}

namespace {

/// Port of legacy `renderZoomedSquare`. Accumulates, into `output`, every source pad that the
/// output square [`start`, `end`) covers on `source`'s row `y`, weighted by how much of the square
/// each covers.
void render_zoomed_square(int32_t start, int32_t end, uint32_t source_times_bigger, uint32_t source_fade,
                          uint32_t* output, const PadGrid& source, uint8_t y, int32_t source_width,
                          bool* drawing_anything) {
	const int32_t start_square_left_edge =
	    static_cast<int32_t>(static_cast<uint32_t>(start) & ~static_cast<uint32_t>(65535));
	for (int32_t source_square_left_edge = std::max<int32_t>(0, start_square_left_edge); true;
	     source_square_left_edge += 65536) {
		if (source_square_left_edge >= end) {
			break;
		}
		const int32_t x_source = source_square_left_edge >> 16;
		if (x_source >= source_width) {
			break;
		}
		const RGB colour = source.at(static_cast<uint8_t>(x_source), y);
		if (!is_lit(colour)) { // legacy's "nothing (i.e. black) at this input pixel" test
			continue;
		}
		*drawing_anything = true;

		const int32_t source_square_right_edge = source_square_left_edge + 65536;
		uint32_t intensity =
		    static_cast<uint32_t>(std::min(source_square_right_edge, end) - std::max(source_square_left_edge, start));
		intensity = static_cast<uint32_t>((static_cast<uint64_t>(intensity) * source_fade * source_times_bigger) >> 32);
		output[0] += static_cast<uint32_t>(colour.r) * intensity;
		output[1] += static_cast<uint32_t>(colour.g) * intensity;
		output[2] += static_cast<uint32_t>(colour.b) * intensity;
	}
}

} // namespace

void zoom(const PadGrid& from, const PadGrid& to, ZoomParams params, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(to.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	DELUGE_GFX_CONTRACT(params.old_zoom > 0 && params.new_zoom > 0);

	// `zoomingIn` is "the new zoom level is finer". Legacy's `zoomMagnitude` is the absolute log2
	// step; zoom levels are ticks-per-square and always differ by a power of two, so the bit-width
	// difference computed here is that same log2 step.
	const bool zooming_in = params.new_zoom < params.old_zoom;
	const int32_t zoom_magnitude =
	    std::abs(static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(params.new_zoom)))
	             - static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(params.old_zoom))));
	// `coarse` below reaches -2^(magnitude - 1), so `increase_magnitude(x, coarse - 14)` shifts
	// right by 2^(magnitude - 1) + 14 -- 30 at magnitude 5, past the operand width at 6. See the
	// note on kMaxZoomMagnitude in tween.h for why that bounds the magnitude.
	DELUGE_GFX_CONTRACT(zoom_magnitude <= kMaxZoomMagnitude);

	// --- renderZoom -----------------------------------------------------------------------
	int32_t transition_progress = params.t;
	if (!zooming_in) {
		transition_progress = 65536 - transition_progress;
	}

	const uint32_t sine_value = static_cast<uint32_t>(
	    (sine_q15(static_cast<uint32_t>(transition_progress + 98304) & 131071, 17) >> 16) + 32768);

	const int32_t powers_of_two = ((transition_progress >> 7) - 512) << zoom_magnitude;
	const int32_t fine = powers_of_two & 1023;
	const int32_t coarse = powers_of_two >> 10;

	int32_t in_image_times_bigger = interpolate_table(static_cast<uint32_t>(fine), 10, kExpTableSmall);
	in_image_times_bigger = increase_magnitude(in_image_times_bigger, coarse - 14);

	// --- renderZoomWithProgress -------------------------------------------------------------
	const PadGrid& inner = zooming_in ? to : from;
	const PadGrid& outer = zooming_in ? from : to;
	const int32_t pin_inner = zooming_in ? params.pin_square_to : params.pin_square_from;
	const int32_t pin_outer = zooming_in ? params.pin_square_from : params.pin_square_to;
	const int32_t pin_screen = params.pin_square_from;
	const uint32_t in_image_fade_amount = sine_value;
	const int32_t main = d.main_cols;

	const uint32_t out_image_times_bigger = static_cast<uint32_t>(in_image_times_bigger) << zoom_magnitude;
	const uint32_t in_image_times_smaller = 4294967295u / static_cast<uint32_t>(in_image_times_bigger);
	const uint32_t out_image_times_smaller = 4294967295u / out_image_times_bigger;

	// Legacy recomputes these whenever `zoomPinSquare[y]` changes from the previous row; with one
	// pin square per frame (the only shape any caller uses) that is once, hoisted out of the loop.
	const int32_t in_image_pos0_onscreen =
	    pin_screen - (pin_inner >> 8) * (in_image_times_bigger >> 8);   // beware rounding inaccuracy (legacy's words)
	const int32_t in_image_left_edge_onscreen = in_image_pos0_onscreen; // innerImageLeftEdge == 0
	const int32_t in_image_right_edge_onscreen = in_image_pos0_onscreen + in_image_times_bigger * main;

	int32_t start_on_in_image[kMaxGridCols];
	int32_t end_on_in_image[kMaxGridCols];
	int32_t start_on_out_image[kMaxGridCols];
	int32_t end_on_out_image[kMaxGridCols];
	uint16_t in_image_fade_per_col[kMaxGridCols];

	for (int32_t x = 0; x < main; x++) {
		const int32_t square_left_edge = x * 65536;
		const int32_t square_right_edge = square_left_edge + 65536;

		int32_t in_image_overlap = std::min(square_right_edge, in_image_right_edge_onscreen)
		                           - std::max(square_left_edge, in_image_left_edge_onscreen);
		if (in_image_overlap < 0) {
			in_image_overlap = 0;
		}
		in_image_fade_per_col[x] =
		    static_cast<uint16_t>((static_cast<uint32_t>(in_image_overlap) * in_image_fade_amount) >> 16);

		const int32_t left_edge_relative_to_pin = pin_screen - square_left_edge;
		const int32_t on_in_relative_to_pin =
		    static_cast<int32_t>((static_cast<int64_t>(left_edge_relative_to_pin) * in_image_times_smaller) >> 16);
		const int32_t on_out_relative_to_pin =
		    static_cast<int32_t>((static_cast<int64_t>(left_edge_relative_to_pin) * out_image_times_smaller) >> 16);

		start_on_out_image[x] = pin_outer - on_out_relative_to_pin;
		start_on_in_image[x] = pin_inner - on_in_relative_to_pin;
		end_on_in_image[x] = start_on_in_image[x] + static_cast<int32_t>(in_image_times_smaller);
		end_on_out_image[x] = start_on_out_image[x] + static_cast<int32_t>(out_image_times_smaller);
	}

	for (uint8_t y = 0; y < d.rows; y++) {
		for (int32_t x = 0; x < main; x++) {
			uint32_t out_value[3] = {0, 0, 0};
			bool drawing_anything = false;

			if (in_image_fade_per_col[x] != 0) {
				render_zoomed_square(start_on_in_image[x], end_on_in_image[x],
				                     static_cast<uint32_t>(in_image_times_bigger), in_image_fade_per_col[x], out_value,
				                     inner, y, main, &drawing_anything);
			}
			render_zoomed_square(start_on_out_image[x], end_on_out_image[x], out_image_times_bigger,
			                     65535u - in_image_fade_per_col[x], out_value, outer, y, main, &drawing_anything);

			RGB result{};
			if (drawing_anything) {
				for (int32_t colour = 0; colour < 3; colour++) {
					result[static_cast<size_t>(colour)] = static_cast<RGB::channel_type>(
					    std::min<uint32_t>(RGB::channel_max, detail::rshift_round(out_value[colour], 16)));
				}
			}
			out.at(static_cast<uint8_t>(x), y) = result;
		}
		// Legacy leaves the sidebar holding whatever `image` already had; the tween has no such
		// ambient buffer, so it passes `from`'s sidebar through (as scroll_horizontal does).
		for (uint8_t x = d.main_cols; x < d.cols(); x++) {
			out.at(x, y) = from.at(x, y);
		}
	}
	out.mark_all_dirty();
}

void explode(const PadGrid& from, ExplodeParams params, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(out.dims() == d);
	// `out.clear()` below runs before `from` is read, so aliasing them would explode a black grid.
	DELUGE_GFX_CONTRACT(&out != &from);
	DELUGE_GFX_CONTRACT(params.from_occupancy.empty() || params.from_occupancy.size() == d.cols());
	// Legacy's `>> (kDisplayWidthMagnitude + 16)`; keeping it a shift keeps the floor rounding.
	DELUGE_GFX_CONTRACT(std::has_single_bit(static_cast<uint32_t>(d.main_cols)));
	const int32_t width_magnitude = std::countr_zero(static_cast<uint32_t>(d.main_cols));

	const int32_t explodedness =
	    params.implode ? (65536 - static_cast<int32_t>(params.t)) : static_cast<int32_t>(params.t);

	out.clear(); // matches legacy's clearing of both the image and its occupancy mask
	uint8_t occupancy[kMaxGridRows][kMaxGridCols];
	std::memset(occupancy, 0, sizeof(occupancy));

	const int32_t main = d.main_cols;
	// These entries are never read -- the xStart/xEnd narrowing below excludes exactly the columns
	// that would leave them unset -- so zeroing is observationally identical to leaving them
	// uninitialised, but removes the stack-garbage hazard a sanitiser would rightly flag.
	int32_t x_dest_array[kMaxGridCols] = {};
	uint16_t x_intensity_array[kMaxGridCols][2] = {};
	int32_t x_start = 0;
	int32_t x_end = main;

	for (int32_t x_source = 0; x_source < main; x_source++) {
		const int32_t x_source_big = x_source << 16;
		int32_t x_origin_big =
		    params.origin_x_big
		    + static_cast<int32_t>((static_cast<int64_t>(params.width_big) * x_source_big) >> (width_magnitude + 16));
		// Snap each pixel's origin-point to an exact square, rounded left, so it matches the arranger.
		x_origin_big &= ~static_cast<int32_t>(65535);

		const int32_t relative_to_origin = x_source_big - x_origin_big;
		const int32_t x_dest_big =
		    x_origin_big + static_cast<int32_t>((static_cast<int64_t>(relative_to_origin) * explodedness) >> 16);

		x_dest_array[x_source] = x_dest_big >> 16;

		if (x_dest_array[x_source] < -1) {
			x_start = x_source + 1;
			continue;
		}
		if (x_dest_array[x_source] >= main) {
			x_end = x_source;
			break;
		}
		x_intensity_array[x_source][1] = static_cast<uint16_t>(x_dest_big);
		x_intensity_array[x_source][0] = static_cast<uint16_t>(65535 - x_intensity_array[x_source][1]);
	}

	// Legacy walks ySource from -1 to kDisplayHeight inclusive, reading the two off-screen rows of
	// `imageStore`. A PadGrid has no off-screen margin, so those two iterations contribute nothing
	// and are simply absent.
	for (int32_t y_source = 0; y_source < d.rows; y_source++) {
		const int32_t y_source_big = y_source << 16;
		const int32_t relative_to_origin = y_source_big - params.origin_y_big;
		const int32_t y_dest_big =
		    params.origin_y_big + static_cast<int32_t>((static_cast<int64_t>(relative_to_origin) * explodedness) >> 16);
		const int32_t y_dest = y_dest_big >> 16;

		uint32_t y_intensity[2];
		y_intensity[1] = static_cast<uint32_t>(y_dest_big & 65535);
		y_intensity[0] = 65535u - y_intensity[1];

		for (int32_t x_source = x_start; x_source < x_end; x_source++) {
			const uint8_t xs = static_cast<uint8_t>(x_source);
			const uint8_t ys = static_cast<uint8_t>(y_source);
			const RGB source_colour = from.at(xs, ys);
			if (!occupied(params.from_occupancy, from, xs, ys)) { // legacy: `if (occupancyMaskStore[...])`
				continue;
			}
			for (int32_t x_offset = 0; x_offset < 2; x_offset++) {
				const int32_t x_now = x_dest_array[x_source] + x_offset;
				if (x_now < 0) {
					continue;
				}
				if (x_now >= main) {
					break;
				}
				for (int32_t y_offset = 0; y_offset < 2; y_offset++) {
					const int32_t y_now = y_dest + y_offset;
					if (y_now < 0) {
						continue;
					}
					if (y_now >= d.rows) {
						break;
					}
					const uint32_t intensity = (y_intensity[y_offset] * x_intensity_array[x_source][x_offset]) >> 16;
					const uint8_t xn = static_cast<uint8_t>(x_now);
					const uint8_t yn = static_cast<uint8_t>(y_now);
					out.at(xn, yn) =
					    draw_square(source_colour, static_cast<int32_t>(intensity), out.at(xn, yn), &occupancy[yn][xn]);
				}
			}
		}
	}
	out.mark_all_dirty();
}

void expand_collapse(const PadGrid& from, const PadGrid& to, ExpandCollapseParams params, PadGrid& out) {
	const auto d = from.dims();
	DELUGE_GFX_CONTRACT(to.dims() == d);
	DELUGE_GFX_CONTRACT(out.dims() == d);
	DELUGE_GFX_CONTRACT(params.row_to < d.rows);
	// `row_from` feeds `going_from * 65536` and then the int8_t narrowing below, exactly as
	// `row_to` does; contract it the same way rather than letting it wrap silently.
	DELUGE_GFX_CONTRACT(params.row_from < d.rows);
	DELUGE_GFX_CONTRACT(params.row_to_by_row.empty() || params.row_to_by_row.size() == d.rows);
	DELUGE_GFX_CONTRACT(params.from_occupancy.empty() || params.from_occupancy.size() == d.cols());
	DELUGE_GFX_CONTRACT(params.to_occupancy.empty() || params.to_occupancy.size() == d.cols());

	// Legacy's `progress` is expandedness; each caller flips it for a collapse. `t` runs forwards,
	// so the flip lives here.
	const int32_t progress =
	    params.collapsing ? (65536 - static_cast<int32_t>(params.t)) : static_cast<int32_t>(params.t);

	// The destination frame plays the part of legacy's freshly-cleared `image` plus the Session
	// pads it drew into it; an occupied pad there starts at full occupancy.
	uint8_t occupancy[kMaxGridRows][kMaxGridCols];
	std::memset(occupancy, 0, sizeof(occupancy));
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			out.at(x, y) = to.at(x, y);
			occupancy[y][x] = occupied(params.to_occupancy, to, x, y) ? 64 : 0;
		}
	}

	uint16_t intensity1_array[kMaxGridRows];
	uint16_t intensity2_array[kMaxGridRows];
	int8_t new_row_position1_array[kMaxGridRows];
	for (int32_t i = 0; i < d.rows; i++) {
		const int32_t going_from = static_cast<int32_t>(params.row_from) + i;
		// `animatedRowGoingTo[i]`: per row when the caller has a span, else one shared destination.
		const int32_t going_to = params.row_to_by_row.empty()
		                             ? static_cast<int32_t>(params.row_to)
		                             : static_cast<int32_t>(params.row_to_by_row[static_cast<size_t>(i)]);
		DELUGE_GFX_CONTRACT(going_to < d.rows);
		const int32_t new_row_position = going_from * 65536 + (going_to - going_from) * (65536 - progress);
		// int8_t, as in legacy. The row preconditions keep this in range, so the narrowing never
		// bites; it is kept so the arithmetic stays literally legacy's.
		new_row_position1_array[i] = static_cast<int8_t>(new_row_position >> 16);
		intensity2_array[i] = static_cast<uint16_t>(new_row_position);
		intensity1_array[i] = static_cast<uint16_t>(65535 - intensity2_array[i]);
	}

	for (uint8_t col = 0; col < d.cols(); col++) {
		for (int32_t i = 0; i < d.rows; i++) {
			const uint8_t src_row = static_cast<uint8_t>(i);
			const RGB square_colours = from.at(col, src_row);
			// legacy: `if (!occupancyMaskStore[i][col]) continue;`
			if (!occupied(params.from_occupancy, from, col, src_row)) {
				continue;
			}
			const int32_t row1 = new_row_position1_array[i];
			if (row1 >= 0 && row1 < d.rows) {
				const uint8_t r = static_cast<uint8_t>(row1);
				out.at(col, r) = draw_square(square_colours, intensity1_array[i], out.at(col, r), &occupancy[r][col]);
			}
			if (row1 >= -1 && row1 < d.rows - 1) {
				const uint8_t r = static_cast<uint8_t>(row1 + 1);
				out.at(col, r) = draw_square(square_colours, intensity2_array[i], out.at(col, r), &occupancy[r][col]);
			}
		}
	}
	out.mark_all_dirty();
}

} // namespace deluge::gfx::grid
