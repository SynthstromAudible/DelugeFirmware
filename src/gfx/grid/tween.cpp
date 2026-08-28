#include "gfx/grid/tween.h"
#include "gfx/contract.h"
#include <algorithm>

namespace deluge::gfx::grid {

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

} // namespace deluge::gfx::grid
