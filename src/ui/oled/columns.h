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

/// One slot of the horizontal menu page: `HorizontalMenu::renderMenuItems`'s view of a
/// `MenuItem` (gui/menu_item/horizontal_menu.cpp:203-307), as plain data plus one callback.
///
/// The legacy renderer asked five questions of every item it drew -- `getOccupiedSlots()`,
/// `showColumnLabel()`, `isItemRelevant()`, `isSubmenu()` and `renderInHorizontalMenu()` -- and
/// those five are exactly the members here. No model type crosses this boundary, and the
/// callback is a function pointer plus a `void*`, so a page of columns is a stack array and
/// nothing is allocated.
///
/// - `label` is legacy's `getColumnLabel()` (horizontal_menu.cpp:608). Spaces are stripped from
///   it here, as `deluge::string::removeSpaces` did on the next line, and it is trimmed from the
///   end until it fits the slot.
/// - `slots` is `getOccupiedSlots()` (:225): the column is `slots * (rect.w / 4)` px wide and
///   costs that many of the page's four slots. The legacy *container* map (:204-212, :231-245)
///   is not ported: a container was only ever a way for several items to share one wide box, and
///   a `Column` with `slots > 1` is that box.
/// - `show_label` is `showColumnLabel()` (:247). Without it the value region gets the whole
///   25px box and the focus highlight covers the whole slot.
/// - `relevant` is `isItemRelevant()` (:223). An irrelevant column draws legacy's dash
///   (:253-257) instead of calling `render`, and the focus walk steps over it. This is legacy's
///   `layout == FIXED` behaviour; a `DYNAMIC` caller drops the column from the span instead,
///   which is what `preparePaging` did for it (:498-500).
/// - `is_submenu` is `isSubmenu()`, read by the highlight rule (:271) and by the label's side
///   ticks (:622). It does *not* draw the arrow -- legacy dispatched that through the virtual
///   `renderInHorizontalMenu`, so it is `submenu_column` below, a `render` like any other.
/// - `render` draws the value region. It is handed a frame whose `(0, 0)` is the region's
///   top-left corner (see `columns` for the region's exact geometry and its one row of
///   headroom) and whether the column has focus. That `focused` argument is "am I the current
///   column", for drawing; the slot *frame* is focused only for that same column, so a renderer
///   built out of `list`/`enum_value`/`toggle` -- none of which take a `focused` parameter -- is
///   inert in every other column. @see ui/core/frame_context.h, rules 2 and 3, for how the two
///   compose.
struct Column {
	std::string_view label;
	uint8_t slots = 1;
	bool show_label = true;
	bool relevant = true;
	bool is_submenu = false;
	void (*render)(OledFrame& slot_frame, bool focused, void* ctx) = nullptr;
	void* ctx = nullptr;
};

/// Where the focused column sits in the page sequence: `HorizontalMenu::Paging`
/// (horizontal_menu.h:34-39) minus its `visiblePageItems` span, which `columns` keeps to itself.
///
/// `total_pages` is 0 for an empty span, exactly as `preparePaging` reports it
/// (horizontal_menu.cpp:537-539), so "no page to draw" and "one page" stay distinguishable.
struct Paging {
	uint8_t page = 0;
	uint8_t total_pages = 0;
	uint8_t focused_on_page = 0;
};

/// `HorizontalMenu::preparePaging` (horizontal_menu.cpp:477-543), as a pure function of the
/// columns and the focused index.
///
/// Legacy walked the items accumulating `getOccupiedSlots()` and started a new page whenever the
/// next item would take the total past four. Two quirks of that loop are reproduced verbatim:
///
///  - the page break is conditional on the item being *relevant* (:504, `... > 4 && is_relevant`),
///    so an irrelevant column that does not fit is appended to the current page rather than
///    starting one, and that page can then hold more than four slots' worth of boxes;
///  - `total_pages` is forced to 0 when the single page acquired no slots at all (:537-539).
///
/// The `readCurrentValue()` call at :495 has no analogue: this layer is handed values, not
/// models.
Paging paging_of(std::span<const Column> cols, int32_t focused);

/// The horizontal four-slot menu page: `HorizontalMenu::renderOLED` (horizontal_menu.cpp:109-135)
/// as one immediate-mode call -- its title (:130), its page counters (:131) and its slots (:132).
///
/// The parameter order is spec B section 6's, `columns(f, rect, ColumnsState&, span<Column>)`,
/// with `title_text` appended from the task brief's interface; passing an empty view draws no
/// title, for a caller that wants to draw its own. **`f.bounds()` is the rect that reproduces
/// legacy** -- unlike `list`, this widget owns the whole panel, including the title band and the
/// counters in the top right corner, so `menu_rect(f)` is *not* its rect (that rect starts below
/// the title, where the boxes start) -- and every golden here is recorded with `f.bounds()`.
/// Everything below is laid out relative to `rect`, so a smaller rect scales the same way, but
/// only the full-frame rect is a port.
///
/// Geometry, all from `renderMenuItems` (:214-306) and frame-local because the legacy
/// `+ OLED_MAIN_TOPMOST_PIXEL` is already in the frame's origin:
///   - `base_y` is `rect.y + kHorizontalMenuBaseY` (:216, a literal `14 +
///     OLED_MAIN_TOPMOST_PIXEL` with no panel-height branch), `column_width` is `rect.w / 4`
///     (:217), a column's box is `column_width * slots` wide (:225) and `kHorizontalBoxHeight`
///     (25) tall (:226);
///   - a labelled column's label occupies the bottom `kTextSpacingY` rows of that box, at
///     `base_y + 25 - 9` (:227-228) -- canvas rows 35..43 on the shipped 48-row panel -- and the
///     value region shrinks to `{x, base_y, box_width - 1, 25 - 9}` (:260-263);
///   - unused slots at the right of the page get legacy's dotted placeholder frames (:290-306);
///   - the page counters are `n/N` in the top right (`renderPageCounters`, :178-201) and the
///     title is drawn with no separator rule (`renderTitle`, :137-176, via `title_paged`).
///
/// **`kHorizontalMenuBaseY`, not `list`'s `kMenuBaseY48/64`.** The task brief's doc sketch named
/// the latter pair, but those are `MenuItem::drawItemsForOled`'s `(OLED_MAIN_HEIGHT_PIXELS == 64) ?
/// 15 : 14` (menu_item.cpp:50), and `renderMenuItems` has no such branch: its base_y is a
/// `constexpr` literal 14 on every panel. The two rules coincide on the shipped 48-row panel and
/// differ by a row on a 64-row one, where legacy's literal is the only reading there is. So this
/// widget deliberately does *not* follow `menu_rect`.
///
/// The value region is handed to `Column::render` as a frame of its own, whose `(0, 0)` is the
/// region's corner. Its clip is the region **grown by one row at the top**: legacy's slot
/// renderers draw at `slot.start_y - 1` (`Number::drawKnob`'s centred arc, number.cpp:129, and
/// `Toggle::renderInHorizontalMenu`, toggle.cpp:62), both blit 16-row icons, and `OledFrame`
/// clips a blit exactly -- so a region clipped tight to the value box would shave the top row off
/// the knob arc and the toggle switch, which legacy draws. The brief asks for
/// `f.region(value_rect)`; this is that region with the one row of bleed the ported renderers
/// demonstrably need, and no more.
///
/// Focus and paging (spec B section 5; legacy moved focus with the instrument buttons, not with
/// an encoder, so there is no legacy turn behaviour to port):
///   - `state.focused` indexes `cols` as a whole, not the page. `f.turn()` walks it one
///     *relevant* column per detent and stops at either end without wrapping, the same walk
///     `list` makes over its selectable rows. `Response::changed` reports that it moved, and
///     requests a repaint.
///   - `state.page` is the page that index lands on, recomputed by `paging_of` and published
///     every frame -- exactly as legacy derived `visiblePageNumber` from the current item
///     (horizontal_menu.cpp:524-535). A turn that crosses a page boundary therefore changes it,
///     and only the new page's columns are drawn. Legacy's page-switch *buttons*
///     (`switchVisiblePage`, :345-366, which wraps) are a scene-level gesture: a scene
///     implements them by moving `state.focused` itself.
///   - `Response::entered` is a select press on a relevant submenu column, which is the press
///     legacy routed to `handleItemAction` -> `soundEditor.enterSubmenu` (:663-678).
///     `Response::focused` is not set, as in `list`: focus lives in the state the scene owns.
///
/// An empty span still draws: legacy's `renderOLED` calls all three of `renderTitle`,
/// `renderPageCounters` and `renderMenuItems` unconditionally (:130-132), and `renderMenuItems`
/// with no items skips straight to the placeholder loop (:290-306) with `current_x == 0`, which
/// draws four dotted frames under the title. `Paging::total_pages` is 0 there (:537-539), so no
/// counters are drawn.
///
/// The focus highlight follows `style.invert_highlight`, which is legacy's
/// `FlashStorage::accessibilityMenuHighlighting` (:268-283): true is `FULL_INVERSION`, an
/// `invertAreaRounded` over the label alone, or over the whole slot when the column has no label
/// or is a submenu (:271-274); false is `PARTIAL_INVERSION`/`NO_INVERSION`, a rule under the slot
/// (:281). A page holding a single 4-slot column is not highlighted at all (:267).
Response columns(OledFrame& f, gfx::oled::Rect rect, ColumnsState& state, std::span<const Column> cols,
                 std::string_view title_text = {});

/// `HorizontalMenu::renderTitle` (horizontal_menu.cpp:137-176): the screen title, with no
/// separator rule (:175 passes `false`), shortened to make room for the page counters.
///
/// When \p paged (legacy `totalPages > 1`) and the title is wider than
/// `f.bounds().w - (kTextSpacingX * 2 + 10)`, a title of *exactly two* space-separated words is
/// redrawn as "F. WORD" -- the first word's initial, a period, a space, the second word
/// (:168-171). Any other word count is drawn unshortened, which is legacy's own behaviour, not an
/// omission.
///
/// The width comparison narrows to `uint8_t` because legacy's does (:142), so a title wider than
/// 255px wraps and is left alone. Kept rather than "fixed": it takes 29 characters of the title
/// face to reach that, no shipped title is close, and a silent behaviour change here would be
/// invisible until some future long title stopped shortening.
void title_paged(OledFrame& f, std::string_view s, bool paged);

/// @{
/// @name Ready-made column renderers
///
/// Four of the `renderInHorizontalMenu` overrides a slot can have, as `Column::render` functions.
/// Each takes its data through `Column::ctx`, which must point at the matching struct and
/// outlive the call. They are plain functions, so a page can mix them freely with a caller's own.
///
/// A fifth arm has no helper here: `Number::renderInHorizontalMenu`'s `default:` case
/// (number.cpp:99-103) draws the raw number in the **title** face
/// (`kTextTitleSpacingX`/`kTextTitleSizeY`), which is what `meter` draws for `Hint::None` -- so a
/// plain number is a `meter_column` with `Hint::None`, *not* a `text_column`, whose face is the
/// row font.

/// `Number::renderInHorizontalMenu` (number.cpp:75-105), i.e. `meter` over the whole value
/// region. `ctx` is a `MeterColumn`.
struct MeterColumn {
	Hint hint = Hint::None;
	float norm = 0.0f;
	int32_t raw = 0;
	/// `getMaxValue()`; only `Hint::Ducking` reads it, and that hint must set it (see `meter`).
	int32_t raw_max = 50;
};
void meter_column(OledFrame& slot_frame, bool focused, void* ctx);

/// `Enumeration::renderInHorizontalMenu` (enumeration.cpp:42-51): the short option name centred
/// across the region at `kHorizontalSlotYOffset`, in the row face, trimmed to fit.
/// `Decimal::renderInHorizontalMenu` (decimal.cpp:176-191) is the same call with a formatted
/// number, so the caller formats and this draws. `ctx` is a `TextColumn`.
struct TextColumn {
	std::string_view value;
};
void text_column(OledFrame& slot_frame, bool focused, void* ctx);

/// `Submenu::renderInHorizontalMenu` (submenu.cpp:49-56): the bold arrow, centred. Takes no
/// `ctx`.
void submenu_column(OledFrame& slot_frame, bool focused, void* ctx);

/// `Toggle::renderInHorizontalMenu` (toggle.cpp:59-63): the switch icon, centred one row above
/// the region. `ctx` is a `ToggleColumn`.
struct ToggleColumn {
	bool on = false;
};
void toggle_column(OledFrame& slot_frame, bool focused, void* ctx);
/// @}

} // namespace deluge::ui::oled
