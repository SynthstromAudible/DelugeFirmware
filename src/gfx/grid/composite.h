#pragma once
#include "gfx/grid/pad_grid.h"
#include <cstdint>

namespace deluge::gfx::grid {

/// How `composite()` combines one `PadGrid` "overlay" onto another "base". Ported from the SDK's
/// `deluge-grid-toolkit::layer::BlendMode` (`layer.rs:17-25`), which composites `GridLayer`s whose
/// cells are `Option<Color>` (`None` = transparent) onto a `Grid`. `PadGrid` has no such per-cell
/// transparency channel -- every cell is a concrete `RGB` -- so black (`RGB{0, 0, 0}`) stands in
/// for "nothing here" wherever the SDK's `None` would have made a cell transparent. That
/// substitution is exact for `Overlay` (the SDK already treats a black *destination* cell as
/// empty, `layer.rs:96-98`) but is a deliberate adaptation for `Replace`, documented per mode
/// below.
enum class BlendMode : uint8_t {
	/// The SDK's `Replace` (`layer.rs:91-93`) overwrites unconditionally for every non-`None`
	/// overlay cell -- transparency alone gates it, not colour. Since `PadGrid` has no
	/// transparency channel, a non-black overlay pad is what stands in for "this cell was
	/// painted", so `Replace` here overwrites `out` with the overlay's colour only where that
	/// colour is non-black; a black overlay pad leaves `base`'s colour showing through. This is
	/// the one place this port's semantics differ from the SDK's -- see the enumerator comment.
	Replace,
	/// Exactly the SDK's `Overlay` (`layer.rs:94-99`): the overlay's colour replaces the base's
	/// only where the base is currently black; a non-black base pad is left alone regardless of
	/// what the overlay holds there (even a non-black overlay colour does not win).
	Overlay,
	/// Exactly the SDK's `Tint` (`layer.rs:100-106`): each channel is `base * overlay / 255`,
	/// integer division truncating (no rounding) -- ported verbatim, including the truncation.
	Tint,
};

/// Composite `overlay` onto `base` per `mode`, writing every cell into `out` (`base`'s and
/// `overlay`'s dimensions must match `out`'s -- `DELUGE_GFX_CONTRACT`, gfx/contract.h -- since a
/// mismatched grid would read or write past one of the three buffers). `out` may alias `base` (a
/// caller compositing in place passes the same `PadGrid&` for both); `overlay` must not alias
/// `out` when `out` and `base` are different objects, since bytes already written to `out` would
/// then be read back as `overlay` input for a later cell -- no call site in this toolkit does
/// that. Rows of `out` whose colour actually changed are marked dirty (`PadGrid::mark_row_dirty`);
/// rows that composite to the same colour they already held are left alone, so a caller that
/// composites every tick does not force a full redraw of an unchanging surface.
void composite(const PadGrid& base, const PadGrid& overlay, BlendMode mode, PadGrid& out);

} // namespace deluge::gfx::grid
