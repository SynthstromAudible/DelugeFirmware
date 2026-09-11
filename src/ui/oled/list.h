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

/// @brief One row of a menu list.
///
/// Plain data: a label, an optional right-aligned value string, an optional right-hand icon (the
/// `>` arrow or a checkbox), and whether the cursor may land on it.
///
/// @note A row with `selectable == false` (e.g. a section heading) is still drawn and the cursor
///       still steps over it, but the cursor can never stop on it and `select_pressed` on it never
///       reports `entered`. A caller that wants a row omitted entirely must filter it out of the
///       span before calling `list`.
struct ListRow {
	std::string_view label;
	std::string_view value = {};
	const gfx::oled::Icon* icon = nullptr;
	bool selectable = true;
};

/// @brief The rect a vertical menu occupies in @p f: the whole frame below the menu base-y.
///
/// @note Frame-local coordinates. The base-y is `style.menu_base_y` (14px on the 48px-tall panel,
///       15px on the 64px-tall one); both shipped panels leave room for exactly `style.menu_rows`
///       rows below it, at `style.row_height` pitch.
/// @param f Frame to measure against.
/// @return The menu rect, in frame-local coordinates.
gfx::oled::Rect menu_rect(const OledFrame& f);

/// @brief How many rows `list` will draw inside @p rect on @p f.
///
/// `min(style.menu_rows, rect.h / style.row_height)`, at least one.
/// @param f    Frame supplying the style metrics.
/// @param rect Rect the rows would be drawn into.
/// @return Number of visible rows, always at least 1.
int32_t list_visible_rows(const OledFrame& f, gfx::oled::Rect rect);

/// @brief The index of the first row `list` shows for a given cursor/row-count/window size.
///
/// Keeps @p cursor as near the middle of a @p visible-row window over @p n rows as the ends of
/// the list allow.
///
/// @note Exposed so a caller that renders only a *slice* of a long list (e.g. `enum_value`, which
///       never materialises more than the visible window) can know which slice `list` would have
///       chosen before building it.
/// @param cursor  Currently selected row index.
/// @param n       Total row count.
/// @param visible Window size in rows.
/// @return Index of the first visible row.
int32_t list_window_start(int32_t cursor, int32_t n, int32_t visible);

/// @brief The vertical menu, as one immediate-mode call.
///
/// @note Ports legacy `MenuItem::drawItemsForOled`, `Submenu::drawSubmenuItemsForOled`,
///       `Selection::drawPixelsForOled` (the window), and `Submenu::selectEncoderAction` (the
///       cursor movement).
///
/// Everything is laid out inside @p rect, in frame-local coordinates:
///   - the first row's top is `rect.y`; rows are `style.row_height` apart and 9 rows tall;
///   - labels start at `rect.x + style.inset_x`;
///   - the highlight, and the per-row band clear, span `rect.x .. rect.right()`;
///   - `list_visible_rows(f, rect)` rows are visible: `min(style.menu_rows, rect.h /
///     style.row_height)`, so a short rect cannot draw past its own bottom edge;
///   - a row with an icon is blitted at `rect.right() - kSubmenuIconSpacingX - 3` and its
///     label/scroller is shortened to leave room for it;
///   - a row with a value right-aligns it against that same content edge and shortens the
///     label by the value's width plus a 4px gap, never past the label origin.
///
/// @note **Two-pass: count then draw.** The row count used for clamping and cursor motion is
///       `rows.size()` -- the span `list` was actually handed *this* frame -- never
///       `state.rows_last`, which is last frame's count. So a list that shrinks under the cursor
///       clamps into range on the same frame it shrinks, and a list that grows lets `turn` walk
///       into the newly-available rows on the same frame too; neither case waits a frame for
///       `state.rows_last` to catch up. `state.rows_last` is set to `rows.size()` afterwards, but
///       purely as a cache for a caller that wants last frame's count (e.g. `enum_value` sizes its
///       own windowed inner `list` call from its own copy of the field) -- `list` itself never
///       reads it back.
///
/// The cursor is clamped into range, then `f.turn()` walks it one selectable row per detent,
/// stopping at the ends without wrapping -- all before anything is drawn. The window keeps the
/// cursor visible per the windowing rule in `list_window_start` and is published as
/// `state.scroll`. `Response.changed` reports that the cursor moved (and requests a repaint);
/// `Response.entered` reports `f.select_pressed()` on a selectable row. `Response.focused` is
/// never set.
///
/// @note `list` claims the turn and the select for the tick whenever its frame is focused; on an
///       unfocused frame it reads no input, moves nothing, and claims nothing -- which is how
///       `enum_value` and `toggle` borrow it for its looks alone.
///
/// The selected row's label goes through `scroll_text` with @p scroll_label, so a label too wide
/// for its box ping-pongs. The highlight follows `style.highlighting`: `!= NoInversion` inverts
/// the whole row (rounded per `style.rounded`); `NoInversion` draws only the left-edge rule and
/// gives `scroll_text` a `+4` clear-only padding, which exists precisely for that case.
///
/// @note The label is drawn *before* the inversion -- the order a full redraw uses. This widget
///       always renders the authoritative full-redraw result at every scroll position; it does
///       not reproduce the two-pixel mid-scroll transient an incremental redraw would leave on
///       the row's right-hand corners between full redraws.
///
/// Each visible row's band is cleared, across the full width of @p rect, before it is drawn.
///
/// @warning Anything else drawn inside @p rect's row bands (a `scrollbar` down its right edge,
///          for instance) must be drawn *after* `list`, or be given a rect of its own that
///          @p rect does not cover -- otherwise the clear erases it.
///
/// Draws no scrollbar; compose with `scrollbar` below if the caller wants one.
///
/// @param f            Frame to draw into and read input from.
/// @param rect         Rect the menu occupies, in frame-local coordinates.
/// @param state        Persistent cursor/scroll-window state.
/// @param rows         The rows to draw this frame.
/// @param scroll_label Scroll state for the selected row's label.
/// @param allow_motion Whether the selected label's scroller may advance this frame.
/// @return `changed` if the cursor moved; `entered` if a selectable row was chosen.
Response list(OledFrame& f, gfx::oled::Rect rect, ListState& state, std::span<const ListRow> rows,
              ScrollText& scroll_label, bool allow_motion = true);

/// @brief A vertical scroll indicator for @p rect.
///
/// A 1px track down `rect.right() - 1` and a thumb `rect.w` wide covering the visible slice of
/// @p total. The thumb is `max(2, rect.h * visible / total)` rows tall at `rect.y + rect.h *
/// first / total`. The track is a hairline at the far right so a `style.scrollbar_w`-wide thumb
/// reads against it; with `rect.w == 1` the two coincide and only the thumb's extent is legible.
/// @param f       Frame to draw into.
/// @param rect    Rect the indicator occupies.
/// @param first   Index of the first visible row.
/// @param visible Number of visible rows.
/// @param total   Total row count.
void scrollbar(OledFrame& f, gfx::oled::Rect rect, int32_t first, int32_t visible, int32_t total);

} // namespace deluge::ui::oled
