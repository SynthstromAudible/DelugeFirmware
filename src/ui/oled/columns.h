#pragma once
#include "gfx/oled/geometry.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include "ui/oled/meters.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

/// @brief One slot of the horizontal menu page: per-item render state as plain data plus one
///        callback.
///
/// @note Ports legacy `HorizontalMenu::renderMenuItems`'s per-`MenuItem` view (`getOccupiedSlots`,
///       `showColumnLabel`, `isItemRelevant`, `isSubmenu`, `renderInHorizontalMenu`) into five
///       plain members. No model type crosses this boundary, and the callback is a function
///       pointer plus a `void*`, so a page of columns is a stack array and nothing is allocated.
///
/// - `label` is the column's title text. Spaces are stripped from it, and it is trimmed from the
///   end until it fits the slot.
/// - `slots` is how many of the page's four slots the column occupies: its box is
///   `slots * (rect.w / 4)` px wide. Legacy let several items share one wide box through a
///   separate container abstraction; a `Column` with `slots > 1` is that box directly.
/// - `show_label` controls whether the value region gets the whole 25px box (false), or gives up
///   its bottom rows to the label (true), which also narrows the focus highlight to the label.
/// - `relevant` marks a column that draws as a dash instead of calling `render`, and is skipped by
///   the focus walk. This is legacy's `layout == FIXED` behaviour; a `DYNAMIC` caller drops the
///   column from the span instead of marking it irrelevant.
/// - `is_submenu` is read by the highlight rule and by the label's side ticks. It does *not* draw
///   the submenu arrow itself -- that is `submenu_column` below, a `render` like any other.
/// - `render` draws the value region. It is handed a frame whose `(0, 0)` is the region's
///   top-left corner (see `columns` for the region's exact geometry and its one row of headroom)
///   and whether the column has focus. That `focused` argument is "am I the current column", for
///   drawing; the slot frame is inert for every other column, so a renderer built out of
///   `list`/`enum_value`/`toggle` -- none of which take a `focused` parameter -- does nothing when
///   it is not the focused column's own slot. @see ui/core/frame_context.h, rules 2 and 3, for how
///   the two compose.
struct Column {
	std::string_view label;
	uint8_t slots = 1;
	bool show_label = true;
	bool relevant = true;
	bool is_submenu = false;
	void (*render)(OledFrame& slot_frame, bool focused, void* ctx) = nullptr;
	void* ctx = nullptr;
};

/// @brief Where the focused column sits in the page sequence.
///
/// @note Mirrors legacy `HorizontalMenu::Paging`, minus the `visiblePageItems` span, which
///       `columns` keeps to itself.
///
/// `total_pages` is 0 for an empty span (not 1), so "no page to draw" and "one page" stay
/// distinguishable.
struct Paging {
	uint8_t page = 0;
	uint8_t total_pages = 0;
	uint8_t focused_on_page = 0;
};

/// @brief Compute which page \p focused falls on, and where it sits within that page.
///
/// @note Ports legacy `HorizontalMenu::preparePaging` as a pure function of the columns and the
///       focused index.
///
/// Columns are packed into pages of four slots, walked in order and accumulating `slots`; a
/// column starts a new page once the running total would exceed four. Two quirks of that packing
/// are preserved:
///
///  - the page break is conditional on the item being *relevant*, so an irrelevant column that
///    does not fit is appended to the current page rather than starting one, and that page can
///    then hold more than four slots' worth of boxes;
///  - `total_pages` is 0 (not 1) when the span acquired no slots at all.
///
/// @param cols     The full column sequence, in page order.
/// @param focused  Index into \p cols of the focused column; out-of-range values are clamped to
///                 the nearest valid column.
/// @return The page containing \p focused, and its position within that page.
Paging paging_of(std::span<const Column> cols, int32_t focused);

/// @brief The column covering instrument button \p button (0-3) on \p focused's visible page.
///
/// @note Ports legacy `HorizontalMenu::handleInstrumentButtonPress`'s search loop
///       (`horizontal_menu.cpp:405-441`): walk the visible page's columns in order, accumulating
///       occupied slots, and take the one whose `[start, start + slots)` covers \p button. The
///       page itself is `window_of(cols, focused)` -- the same page computation `paging_of` and
///       `columns` already use, reused rather than re-derived, and relevance plays no part here:
///       legacy's `layout == FIXED && !isItemRelevant(item)` refusal is the caller's decision (a
///       button press "does nothing" on a disabled column, it does not report "no column"), not
///       this function's.
///
/// @param cols     The full column sequence, in page order.
/// @param focused  Index into \p cols of the currently focused column -- names which page is
///                 visible, exactly as it does for `paging_of`.
/// @param button   Which instrument button (Synth 0, Kit 1, Midi 2, Cv 3).
/// @return The covering column's index, or -1 if \p button lands past the page's occupied slots.
int32_t column_under_button(std::span<const Column> cols, int32_t focused, int32_t button);

/// @brief Where focus lands after switching the visible page by \p direction.
///
/// @note Ports legacy `HorizontalMenu::switchVisiblePage` (`horizontal_menu.cpp:345-366`) plus the
///       `selectMenuItem` call it makes (`:444-476`): the *target* page wraps
///       (`(page + direction) mod total_pages`), and the column selected on it is the one
///       `selectMenuItem` keeps updating to the last *relevant* column seen while walking that
///       page up to (and including) \p focused's own position on its own page --
///       `Paging::selectedItemPositionOnPage`, i.e. `paging_of(cols, focused).focused_on_page`.
///       Falling short of that position (a shorter target page) leaves the last relevant column
///       seen as the answer, matching legacy's own fall-through when the loop's `break` never
///       fires for that page. The page break within the walk is `window_of`'s own rule (gated on
///       `Column::relevant`), not `selectMenuItem`'s ungated one -- one page computation reused
///       throughout, as `column_under_button` above also does.
///
/// @param cols      The full column sequence, in page order.
/// @param focused   Index into \p cols of the currently focused column.
/// @param direction +1 (legacy's CROSS_SCREEN_EDIT) or -1 (SCALE_MODE).
/// @return The column to focus on the target page, or \p focused unchanged when there is only one
///         page (legacy's `totalPages <= 1` early return).
int32_t page_switch(std::span<const Column> cols, int32_t focused, int32_t direction);

/// @brief The horizontal four-slot menu page: title, page counters and column slots, drawn as one
///        immediate-mode call.
///
/// @note Ports legacy `HorizontalMenu::renderOLED`.
///
/// @note **`f.bounds()` is the rect that reproduces legacy geometry.** Unlike `list`, this widget
///       owns the whole panel, including the title band and the page counters in the top right
///       corner, so `menu_rect(f)` is *not* its rect -- that rect starts below the title, where
///       the column boxes start. Everything below is laid out relative to \p rect, so a smaller
///       rect scales the same way, but only the full-frame rect reproduces legacy pixel-for-pixel.
///       Passing an empty \p title_text draws no title, for a caller that wants to draw its own.
///
/// Geometry, all frame-local:
///   - `base_y` is `rect.y + kHorizontalMenuBaseY`; `column_width` is `rect.w / 4`; a column's box
///     is `column_width * slots` wide and `kHorizontalBoxHeight` (25) tall.
///   - a labelled column's label occupies the bottom `kTextSpacingY` rows of that box, and the
///     value region shrinks accordingly.
///   - unused slots at the right of the page get dotted placeholder frames.
///   - the page counters are `n/N` in the top right, and the title has no separator rule (see
///     `title_paged`).
///
/// @note **`kHorizontalMenuBaseY` applies on every panel height**, unlike `list`'s
///       `kMenuBaseY48`/`kMenuBaseY64`: legacy's base_y is a literal `14` with no panel-height
///       branch, so this widget deliberately does not follow `menu_rect`'s 48/64 split. The two
///       rules coincide on the shipped 48-row panel and differ by a row on a 64-row one.
///
/// @note The value region handed to `Column::render` is grown by **one extra row of clip at the
///       top** (its origin is unchanged): two of the ready-made slot renderers -- the number
///       knob's centred arc and the toggle switch icon -- draw a 16-row icon one row above the
///       value box, and a tight clip would shave that row off.
///
/// Focus and paging:
///   - `state.focused` indexes \p cols as a whole, not the page. This control does not move it --
///     legacy's select encoder edits the focused column's value and never moves focus
///     (`HorizontalMenu::selectEncoderAction`), and the instrument buttons move it instead
///     (`HorizontalMenu::buttonAction`, `column_under_button`/`page_switch` above). A scene moves
///     focus by writing `state.focused` itself; `Response::changed` reports only the clamp below,
///     and still requests a repaint when it fires.
///   - `state.focused` is clamped into `[0, cols.size())` every call, so a column list that
///     shrank under a stale index is pulled back into range with no input at all -- the same
///     bound `list` puts on its cursor.
///   - `state.page` is the page that index lands on, recomputed by `paging_of` and published
///     every frame, so a scene that moves `state.focused` across a page boundary sees `state.page`
///     follow it, and only the new page's columns are drawn.
///   - `Response::entered` is a select press on a relevant submenu column. `Response::focused` is
///     not set, as in `list`: focus lives in the state the scene owns.
///
/// @note An empty \p cols still draws: the placeholder loop still runs, drawing four dotted
///       frames under the title, and `Paging::total_pages` is 0, so no counters are drawn.
///
/// The focus highlight follows `style.invert_highlight`: true inverts (rounded) over the label
/// alone, or over the whole slot when the column has no label or is a submenu; false draws a rule
/// under the slot instead. A page holding a single 4-slot column is never highlighted.
///
/// @param f          Target frame.
/// @param rect       Region to lay the page out in; `f.bounds()` for the legacy-reproducing rect.
/// @param state      Persisted focus/page state, updated in place.
/// @param cols       The page's columns, in order.
/// @param title_text Screen title; an empty view draws no title.
/// @return `changed` if focus moved this frame; `entered` if a submenu column was just selected.
Response columns(OledFrame& f, gfx::oled::Rect rect, ColumnsState& state, std::span<const Column> cols,
                 std::string_view title_text = {});

/// @brief The screen title, with no separator rule, shortened to make room for the page counters
///        when needed.
///
/// @note Ports legacy `HorizontalMenu::renderTitle`.
///
/// When \p paged is true and the title is wider than `f.bounds().w - (kTextSpacingX * 2 + 10)`, a
/// title of *exactly two* space-separated words is redrawn as "F. WORD" -- the first word's
/// initial, a period, a space, the second word. Any other word count is drawn unshortened, which
/// is legacy's own behaviour, not an omission.
///
/// @note The width comparison narrows to `uint8_t`, so a title wider than 255px wraps and is left
///       unshortened. Kept rather than fixed: no shipped title is anywhere close to that width,
///       and a silent behaviour change here would be invisible until some future long title
///       stopped shortening.
///
/// @param f     Target frame.
/// @param s     Title text.
/// @param paged Whether the page counters are on screen and competing for width.
void title_paged(OledFrame& f, std::string_view s, bool paged);

/// @name Ready-made column renderers
/// @{
///
/// Four of the `renderInHorizontalMenu` overrides a slot can have, as `Column::render` functions.
/// Each takes its data through `Column::ctx`, which must point at the matching struct and
/// outlive the call. They are plain functions, so a page can mix them freely with a caller's own.
///
/// @note A fifth arm has no helper here: legacy's `Number::renderInHorizontalMenu` `default:` case
///       draws a raw number in the **title** face, which is what `meter` draws for `Hint::None` --
///       so a plain number is a `meter_column` with `Hint::None`, *not* a `text_column`, whose
///       face is the row font.

/// @brief `ctx` type for `meter_column`: draws `meter` over the whole value region.
///
/// @note Ports legacy `Number::renderInHorizontalMenu`.
struct MeterColumn {
	Hint hint = Hint::None;
	float norm = 0.0f;
	int32_t raw = 0;
	/// Only `Hint::Ducking` reads this; that hint must set it (see `meter`).
	int32_t raw_max = 50;
};

/// @brief `Column::render` that draws `ctx` (a `MeterColumn`) as a meter over the whole value
///        region.
///
/// @param slot_frame Value-region frame to draw into.
/// @param focused    Whether this column currently has focus.
/// @param ctx        Must point at a `MeterColumn`.
void meter_column(OledFrame& slot_frame, bool focused, void* ctx);

/// @brief `ctx` type for `text_column`: the short option/value text.
///
/// @note Ports legacy `Enumeration::renderInHorizontalMenu` and `Decimal::renderInHorizontalMenu`
///       (the caller formats the number; this just draws text).
struct TextColumn {
	std::string_view value;
};

/// @brief `Column::render` that draws `ctx` (a `TextColumn`) centred across the region, trimmed to
///        fit, in the row face.
///
/// @param slot_frame Value-region frame to draw into.
/// @param focused    Whether this column currently has focus.
/// @param ctx        Must point at a `TextColumn`.
void text_column(OledFrame& slot_frame, bool focused, void* ctx);

/// @brief `Column::render` that draws the bold submenu arrow, centred. Takes no `ctx`.
///
/// @note Ports legacy `Submenu::renderInHorizontalMenu`.
///
/// @param slot_frame Value-region frame to draw into.
/// @param focused    Whether this column currently has focus.
/// @param ctx        Unused.
void submenu_column(OledFrame& slot_frame, bool focused, void* ctx);

/// @brief `ctx` type for `toggle_column`: the switch state.
///
/// @note Ports legacy `Toggle::renderInHorizontalMenu`.
struct ToggleColumn {
	bool on = false;
};

/// @brief `Column::render` that draws the on/off switch icon, centred one row above the region.
///
/// @param slot_frame Value-region frame to draw into.
/// @param focused    Whether this column currently has focus.
/// @param ctx        Must point at a `ToggleColumn`.
void toggle_column(OledFrame& slot_frame, bool focused, void* ctx);
/// @}

} // namespace deluge::ui::oled
