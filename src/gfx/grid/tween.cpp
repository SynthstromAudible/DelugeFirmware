#include "gfx/grid/tween.h"
#include <algorithm>

namespace deluge::gfx::grid {

void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out) {
	const auto d = from.dims();
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
	const int32_t main = d.main_cols;
	// A scroll by the full main-axis extent already yields an all-`incoming` result; anything
	// larger has no further meaning and would index PadGrid::at's uint8_t params out of range.
	offset = std::clamp<int32_t>(offset, -main, main);
	for (uint8_t y = 0; y < d.rows; y++) {
		for (int32_t x = 0; x < main; x++) {
			const int32_t src = x - offset;
			if (src >= 0 && src < main) {
				out.at(x, y) = from.at(src, y);
			}
			else if (offset > 0) {
				out.at(x, y) = incoming.at(main + src, y); // src < 0: incoming's right edge
			}
			else {
				out.at(x, y) = incoming.at(src - main, y); // src >= main: incoming's left edge
			}
		}
		for (int32_t x = main; x < d.cols(); x++) {
			out.at(x, y) = from.at(x, y);
		}
	}
	out.mark_all_dirty();
}

void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out) {
	const auto d = from.dims();
	const int32_t rows = d.rows;
	// Same reasoning as scroll_horizontal's clamp: a full-extent scroll is already all-`incoming`.
	offset = std::clamp<int32_t>(offset, -rows, rows);
	for (int32_t y = 0; y < rows; y++) {
		const int32_t src = y - offset;
		for (uint8_t x = 0; x < d.cols(); x++) {
			if (src >= 0 && src < rows) {
				out.at(x, y) = from.at(x, src);
			}
			else if (offset > 0) {
				out.at(x, y) = incoming.at(x, rows + src);
			}
			else {
				out.at(x, y) = incoming.at(x, src - rows);
			}
		}
	}
	out.mark_all_dirty();
}

void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out) {
	const auto d = in.dims();
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			RGB c = in.at(x, y);
			out.at(x, y) = (x < d.main_cols) ? c.greyOut(proportion) : c;
		}
	}
	out.mark_all_dirty();
}

} // namespace deluge::gfx::grid
