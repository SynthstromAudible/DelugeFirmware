#pragma once
#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

/// One row of a menu list. Plain data: a label, an optional right-aligned value string, an
/// optional right-hand icon (the legacy "submenu item type": the `>` arrow or a checkbox), and
/// whether the cursor may land on it.
///
/// `selectable == false` is this layer's stand-in for the legacy `MenuItem::isRelevant`
/// filter. Legacy never *drew* an irrelevant item -- `Submenu::drawPixelsForOled` collects only
/// relevant items into its window (submenu.cpp:60-76) and `Submenu::selectEncoderAction`
/// consumes a detent only on a relevant item, reverting to the last relevant one when it runs
/// off the end (submenu.cpp:132-176). A caller that wants the legacy behaviour exactly still
/// filters the rows out before building the span; `selectable == false` is for the rows legacy
/// had no concept of (section headings, greyed-out entries), and it reproduces the
/// `selectEncoderAction` scan: the row is drawn, the cursor steps over it, and `select_pressed`
/// on it never reports `entered`.
struct ListRow {
	std::string_view label;
	std::string_view value = {};
	const gfx::oled::Icon* icon = nullptr;
	bool selectable = true;
};

/// The legacy vertical menu, as one immediate-mode call.
///
/// Ports `MenuItem::drawItemsForOled` (menu_item.cpp:47-66) and
/// `Submenu::drawSubmenuItemsForOled` (submenu.cpp:98-127) for the drawing,
/// `Selection::drawPixelsForOled` (selection.cpp:9-29) for the window, and
/// `Submenu::selectEncoderAction` (submenu.cpp:132-176) for the cursor movement.
///
/// Geometry, all frame-local (the frame's origin already carries `visible_top`, which is the
/// legacy `+= OLED_MAIN_TOPMOST_PIXEL` at menu_item.cpp:51):
///   - the first row's top is `kMenuBaseY64` on a 64-row panel and `kMenuBaseY48` otherwise,
///     selected by `f.canvas.geometry().height` -- legacy's
///     `(OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14` (menu_item.cpp:50);
///   - rows are `kTextSpacingY` apart and 9 rows tall (`yPixel .. yPixel + 8`);
///   - labels start at `x = kTextSpacingX`;
///   - `style.menu_rows` rows are visible -- legacy's `OLED_HEIGHT_CHARS - 1`, which is also
///     the `kOLEDMenuNumOptionsVisible` the window rule is written against
///     (definitions_cxx.hpp:957);
///   - a row with an icon blits it at `x = bounds().w - kSubmenuIconSpacingX - 3`
///     (`MenuItem::getSubmenuItemTypeRenderIconStart`, menu_item.h:280) and shortens its
///     label/scroller to `bounds().w - (4 + kSubmenuIconSpacingX + 3)`
///     (`getSubmenuItemTypeRenderLength`, menu_item.h:278);
///   - a row with a value right-aligns it against that same content edge and shortens the
///     label by the value's width plus the legacy 4px "spacing before". No legacy menu row
///     drew a value string, so this arrangement is new; everything else above is a port.
///
/// Behaviour: `input.turn` is applied first, walking one selectable row per detent and
/// stopping at the ends without wrapping, exactly as `Submenu::selectEncoderAction` does; the
/// walk is bounded by the row count the *previous* frame saw (`state.rows_last`, or
/// `rows.size()` on the first frame), so a list that shrank under the cursor cannot be walked
/// past its old length. `state.rows_last` is then set to `rows.size()`. The window keeps the
/// cursor visible per `Selection`'s pos/tail rule and is published as `state.scroll`.
/// `Response.changed` reports that the cursor moved (and requests a repaint);
/// `Response.entered` reports `input.select_pressed` on a selectable row.
///
/// The selected row's label goes through `scroll_text` with `scroll_label`, so a label too wide
/// for its box ping-pongs -- the legacy `setupSideScroller(0, name, kTextSpacingX, endX, yPixel,
/// yPixel + 8, ..., true)` at menu_item.cpp:62 / submenu.cpp:124. The highlight follows
/// `style.invert_highlight`, which is `Canvas::invertLeftEdgeForMenuHighlighting`'s
/// `menuHighlightingInverts` (canvas.cpp:26-33): true inverts the whole row, rounded per
/// `style.rounded` because legacy routes it through `Canvas::invertAreaRounded(...,
/// roundedCornersEnabled)` (oled_canvas/canvas.h:136-138); false draws only the left-edge rule
/// and gives `scroll_text` legacy's clear-only `+4` padding (oled.cpp:984-992), which exists
/// precisely for that case. The label is drawn *before* the inversion, which is the order a
/// legacy full redraw uses; legacy's scroller-*tick* path instead re-inverts just its own box
/// (oled.cpp:996-997), erasing the row's right-hand rounded corners until the next full redraw,
/// and that transient is deliberately not reproduced.
///
/// Each visible row's 9-row band is cleared before it is drawn. Legacy got the same effect from
/// `renderUIsForOled` clearing the whole canvas before `drawPixelsForOled`; an immediate-mode
/// widget owns its own band instead.
///
/// Draws no scrollbar: neither legacy menu renderer has one. Spec B section 6's catalogue row
/// says `list` "draws `scrollbar` when rows > visible", which would break pixel parity, so the
/// caller composes the two if it wants one -- see `scrollbar` below, whose own catalogue row
/// already says "for non-list views".
Response list(OledFrame& f, ListState& state, std::span<const ListRow> rows, ScrollText& scroll_label,
              bool allow_motion = true);

/// A vertical scroll indicator for `rect`: a 1px track down `rect.right() - 1` and a thumb
/// `rect.w` wide covering the visible slice of `total`.
///
/// The thumb is `max(2, rect.h * visible / total)` rows tall at `rect.y + rect.h * first /
/// total`. There is no legacy scrollbar anywhere in the firmware to port -- the menus scroll
/// without an indicator -- so this is the brief's formula, unmodified. The track is a hairline
/// at the far right so a `style.scrollbar_w`-wide thumb reads against it; with `rect.w == 1`
/// the two coincide and only the thumb's extent is legible.
void scrollbar(OledFrame& f, gfx::oled::Rect rect, int32_t first, int32_t visible, int32_t total);

} // namespace deluge::ui::oled
