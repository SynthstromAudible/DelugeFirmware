#include "gfx/grid/composite.h"

#include "gfx/contract.h"

namespace deluge::gfx::grid {

namespace {

constexpr bool is_black(RGB c) {
	return c.r == 0 && c.g == 0 && c.b == 0;
}

/// Ports legacy `GridCompositor::apply_layer`'s per-cell blend-mode dispatch. The SDK skips a cell
/// entirely when the overlay's `Option<Color>` is `None`; here every overlay cell is a concrete
/// `RGB`, so Replace uses `is_black(overlay)` as that same "nothing to draw" test (documented on
/// the enumerator in composite.h) while Overlay and Tint are the SDK's formulas verbatim.
RGB blend_cell(RGB base, RGB overlay, BlendMode mode) {
	switch (mode) {
	case BlendMode::Replace:
		// Adapted: `None` (skip) becomes "overlay is black" (see composite.h).
		return is_black(overlay) ? base : overlay;
	case BlendMode::Overlay:
		// Verbatim: overlay wins only where the base is currently black.
		return is_black(base) ? overlay : base;
	case BlendMode::Tint:
		// Verbatim: `((dst.c as u16 * src.c as u16) / 255) as u8` per channel -- truncating
		// integer division, not rounded.
		return RGB{
		    .r = static_cast<RGB::channel_type>((static_cast<uint16_t>(base.r) * overlay.r) / 255),
		    .g = static_cast<RGB::channel_type>((static_cast<uint16_t>(base.g) * overlay.g) / 255),
		    .b = static_cast<RGB::channel_type>((static_cast<uint16_t>(base.b) * overlay.b) / 255),
		};
	}
	__builtin_unreachable();
}

} // namespace

void composite(const PadGrid& base, const PadGrid& overlay, BlendMode mode, PadGrid& out) {
	// GridDimensions::operator== (gfx/grid/pad_grid.h) is exactly "do these three grids share one
	// shape" -- the precondition every cell access below relies on to stay in range.
	DELUGE_GFX_CONTRACT(base.dims() == overlay.dims());
	DELUGE_GFX_CONTRACT(base.dims() == out.dims());
	const GridDimensions d = out.dims();
	for (uint8_t y = 0; y < d.rows; y++) {
		bool row_changed = false;
		for (uint8_t x = 0; x < d.cols(); x++) {
			// Read base/overlay (and, if `out` aliases `base`, that read *is* the current `out`
			// cell) before writing, so aliasing `base` into `out` is safe regardless of scan order:
			// every output cell depends only on the same-position base/overlay cells, never on a
			// neighbour's already-written result.
			const RGB result = blend_cell(base.at(x, y), overlay.at(x, y), mode);
			RGB& cell = out.at(x, y);
			if (!(cell == result)) {
				cell = result;
				row_changed = true;
			}
		}
		if (row_changed) {
			out.mark_row_dirty(y);
		}
	}
}

} // namespace deluge::gfx::grid
