#pragma once
#include "gfx/oled/geometry.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/core/style.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

/// The bounds an edited value is held inside, and the amount one detent of the primary edit
/// encoder moves it. `min`/`max` are legacy's `getMinValue()`/`getMaxValue()`; `step` has no
/// legacy counterpart on `Integer` (whose `selectEncoderAction` adds the raw offset,
/// integer.cpp:25-39) and exists for the call sites that legacy gave a coarse step by other
/// means -- see the fine/coarse note on `int_value`.
struct Range {
	int32_t min, max, step = 1;
};

/// The presentation of a plain number: legacy `Integer::getUnit()` and the `IntegerWithOff`
/// subclass, as data rather than as two virtuals.
struct ValueStyle {
	/// Appended to the digits, truncated to 4 characters -- legacy `strncat(buffer, getUnit(), 4)`
	/// (integer.cpp:55).
	std::string_view unit = {};
	/// Draw "OFF" instead of a `0`, exactly as `IntegerWithOff::drawPixelsForOled`
	/// (integer.cpp:41-50).
	bool off_at_zero = false;
};

/// The largest window `enum_value` materialises on the stack.
constexpr int32_t kMaxWindowRows = 8;
static_assert(kMaxWindowRows >= kDelugeStyle48.menu_rows && kMaxWindowRows >= kDelugeStyle64.menu_rows,
              "the stack window must cover every shipped profile's menu_rows: a profile asking for "
              "more rows than the cap would have list draw the capped count and leave the rest of "
              "the panel holding stale content");

/// The huge centred number of `Integer::drawPixelsForOled` (integer.cpp:59-61), which is
/// `drawInteger(kTextHugeSpacingX, kTextHugeSizeY, 18)` -- i.e. `drawStringCentred(buffer,
/// 18 + OLED_MAIN_TOPMOST_PIXEL, 18, 20)` (integer.cpp:52-57). The `+ OLED_MAIN_TOPMOST_PIXEL`
/// is the frame's origin, so the layout constant here is the frame-local `kValueY == 18`.
///
/// \p rect is spec B section 6's second parameter (`int_value(f, rect, int32_t&, Range,
/// ValueStyle)`), and the layout is relative to it: the digits occupy the `kTextHugeSizeY`-tall
/// band at `rect.y + kValueY` and are centred across `rect.w`. **`f.bounds()` is the rect that
/// reproduces legacy** -- legacy centred over the whole panel (`drawStringCentred`'s default
/// `centrePos` is `OLED_MAIN_WIDTH_PIXELS / 2`) and measured its `18` from the top of the panel
/// -- and every golden here is recorded with it.
///
/// The face is `f.style.value_font`; both shipped profiles set `MetricBold20`, whose metrics
/// are exactly the `kTextHugeSpacingX` / `kTextHugeSizeY` pair legacy hard-coded, so the pixels
/// are unchanged.
///
/// The band the digits occupy is cleared first, across `rect.w`. Legacy got the same effect from
/// `renderUIsForOled` clearing the whole canvas before `drawPixelsForOled`; an immediate-mode
/// widget owns its own band instead.
///
/// Editing, only while \p focused (spec B section 3: `edit[]` changes the focused value, with no
/// modal edit state). NB two different "focused"s meet here and compose: \p focused is "am I the
/// current control in my pane", `OledFrame::focused` is "is my pane live", and the widget reads
/// input only when both hold -- @see ui/core/frame_context.h, rules 2 and 3.
///   - `f.edit(0)` (GoldUpper) moves the value by `range.step` per detent -- the coarse
///     knob;
///   - `f.edit(1)` (GoldLower) moves it by one unit per detent -- the fine knob.
/// The coarse/fine split is legacy's only precedent for two edit granularities on one number,
/// `Decimal::getNumberEditSize` (decimal.cpp:49-55), which likewise pairs a coarse step with a
/// 1-unit fine one; `Integer::selectEncoderAction` itself has a single encoder and a `step` of 1
/// (integer.cpp:25-39), for which the two arms coincide. NB `decimal_value`'s coarse amount is
/// `10^edit_pos` and `range.step` is its *fine* amount, so `step` plays opposite roles in the two
/// widgets -- they coincide only at the default `step == 1`.
///
/// The sum is added and then clamped to `[range.min, range.max]` (integer.cpp:27-36); the value is
/// *not* snapped to a multiple of `step`, because legacy never snapped. `Response::changed`
/// reports that the value actually moved -- a detent into a clamped end changes nothing and
/// reports nothing -- and requests a repaint. `Response::focused` echoes \p focused.
Response int_value(OledFrame& f, gfx::oled::Rect rect, int32_t& value, Range range, ValueStyle style = {},
                   bool focused = true);

/// `Decimal::drawPixelsForOled` (decimal.cpp:128-164): the value's digits in the huge font with
/// a half-width period \p places from the right, and a blinking underline cursor under the digit
/// at \p edit_pos.
///
/// **Canvas-absolute y, reproduced deliberately, and NOT relative to \p rect.** Every other menu
/// renderer offsets its y by `OLED_MAIN_TOPMOST_PIXEL`; this one does not -- the digits go to a
/// literal `20` (decimal.cpp:149, 152, 161) and the cursor to a literal `41`/`42`
/// (decimal.cpp:163), in canvas rows, so on the 48-row panel with its 5 hidden rows the digits sit
/// 5 rows *higher* relative to the visible area than `int_value`'s do. Those two constants are
/// converted back to frame-local by subtracting `f.origin.y`, and `rect.y`/`rect.h` are ignored:
/// there is no sane way to make a canvas-absolute constant relative to a rect, and pretending
/// otherwise would silently move the digits the moment anyone passed a rect that is not the whole
/// frame. \p rect is honoured horizontally -- the string is centred across `rect.w`, clipped at
/// `rect.right()` and the cleared bands span `rect.x .. rect.right()` -- which is the whole of
/// what legacy's `OLED_MAIN_WIDTH_PIXELS` (decimal.cpp:142, 149) meant. **`f.bounds()` is the
/// rect that reproduces legacy**, and the goldens are recorded with it. A caller that wants a
/// decimal inside a pane needs a different widget, not a different rect.
///
/// There is no legacy 64-row behaviour to match: `src/board_config.h:62-64` defines
/// `OLED_MAIN_HEIGHT_PIXELS 48` and `OLED_MAIN_TOPMOST_PIXEL 5` unconditionally, no build ever set
/// height 64, and the `== 64` arms elsewhere (integer.cpp:65, toggle.cpp:26) are dead
/// alternatives. Keeping the constant absolute on a 64-row profile is therefore the only reading
/// there is, rather than a choice between two legacy behaviours.
///
/// Layout, verbatim from decimal.cpp:129-161: the digits are drawn with `use_width` set, so each
/// advances exactly `kTextHugeSpacingX` (18px) and the period exactly half that (9px) --
/// monospaced, unlike every other string in the toolkit. `length` is the width of
/// `intToString(value, buffer, places + 1)`: the digits of |value| zero-padded to `places + 1`,
/// with a leading `-` for negatives. The digits' band (`kTextHugeSizeY` rows at canvas y 20) and
/// the cursor's (2 rows at canvas y 41) are both cleared before either is drawn; canvas row 40,
/// between them, belongs to nobody, where legacy got a whole-canvas clear from `renderUIsForOled`.
///
/// The cursor is the `kFlashTime` blink of `OLED::setupBlink` / `performBlink`
/// (oled.cpp:832-849): a 2-row bar at canvas rows 41..42, `kTextHugeSpacingX - 2` px wide,
/// starting one pixel right of the edited digit's own start (decimal.cpp:163), drawn by
/// inverting that band. Legacy's `uiTimerManager` half-period becomes \p blink plus
/// `gate.request_repaint_after`, and legacy's `shouldBlinkImmediately` becomes "reset the phase to
/// lit". A full legacy redraw re-armed the timer without inverting, so the cursor starts *dark*
/// for one `kFlashTime` after the value changes or the widget first renders; that is reproduced.
///
/// `blink` is (re)armed by this widget: `Blink::period_ms` holds `kFlashTime` and
/// `Blink::until_ms` holds the phase *anchor* (see `ui/core/state.h`) -- the ms timestamp the
/// current blink cycle was last restarted at -- with the cursor lit while
/// `((now - until_ms) / period_ms)` is odd. Spec B section 3 declares `Blink`'s two fields without
/// assigning them meanings, and this is the only meaning that fits legacy's free-running square
/// wave into two words with no phase bit; `period_ms != kFlashTime` doubles as "this Blink has not
/// been armed for a decimal cursor yet", which means a `Blink` shared with a widget that wants a
/// different period would be re-anchored on every frame. One `Blink` per cursor.
///
/// `f.horizontal()` moves \p edit_pos, one place per non-zero tick, exactly as
/// `Decimal::horizontalEncoderAction` (decimal.cpp:83-101): a positive tick moves the cursor
/// right (`edit_pos--`, floored at 0), a negative tick moves it left (`edit_pos++`) only while
/// the next place up is still reachable inside `range.max` (legacy `numberEditSize * 10 <=
/// getMaxValue()`). **Any** tick restarts the blink lit, including one that hits either of those
/// bounds and moves nothing: legacy sets `movingCursor = true` at decimal.cpp:98, after and
/// outside the `if/else` at :84-96, and then renders, so `setupBlink(..., true)` (:163) inverts
/// the band immediately either way.
///
/// `f.edit(0)` moves the value by `10^edit_pos` per detent -- legacy's `numberEditSize`
/// (decimal.cpp:39-42, 58) -- and `f.edit(1)` by `range.step`, the fine amount. Both only
/// while \p focused. The clamp is `add_clamped`'s, i.e. both ends at once;
/// `Decimal::selectEncoderAction` clamps only in the turn's own direction (decimal.cpp:60-74),
/// which differs solely when the incoming value already sits outside `[min, max]` and the turn is
/// away from the violated bound -- legacy would leave it out of range there, this pulls it in.
/// (`Integer::selectEncoderAction` does clamp both ways, integer.cpp:27-36, so `int_value` is
/// exact.) Legacy's `scrollToGoodPos` window is not ported: `drawPixelsForOled` never reads
/// `numberScrollAmount`, so it has no effect on any pixel.
Response decimal_value(OledFrame& f, gfx::oled::Rect rect, int32_t& value, Range range, uint8_t places,
                       int8_t& edit_pos, Blink& blink, bool focused = true);

/// The windowed name list of `Selection::drawPixelsForOled` (selection.cpp:8-30), as one call:
/// `list` over \p rect with one row per name and the cursor on \p index. **`menu_rect(f)` is the
/// rect that reproduces legacy** (it is the rect `Selection` -> `MenuItem::drawItemsForOled`
/// lays out in), and the golden is recorded with it; everything below is relative to it, because
/// `list` already is.
///
/// The window is `list`'s (which is `Selection`'s own pos/tail rule, already ported); the cursor
/// movement is `Enumeration::selectEncoderAction` (enumeration.cpp:18-36): `index + turn`,
/// wrapped into `[0, n)` when \p wrap and clamped to it otherwise. `Enumeration::wrapAround`
/// returns false by default (enumeration.cpp:12-16), which is why \p wrap defaults to false.
/// `list`'s own clamping walk is bypassed -- it is `Submenu`'s, not `Enumeration`'s, and cannot
/// wrap -- by rendering `list` through an *unfocused* frame: it draws the window and the
/// highlight and reads no input at all (ui/core/frame_context.h rule 3). The press is reported by
/// `enum_value` itself, which has claimed both the turn and the select for the tick.
///
/// Spec B section 6's catalogue row also says "name + arrows". `Selection::drawPixelsForOled`
/// draws no arrows -- it hands the options straight to `MenuItem::drawItemsForOled`
/// (selection.cpp:29), which draws labels and the highlight and nothing else -- so none are drawn
/// here either, on the same footing as `list` declining spec section 6's scrollbar.
///
/// \p state carries the widget's published position: `cursor` is \p index, `scroll` the first
/// visible name, `rows_last` the name count. Only the visible slice of \p names is materialised
/// as `ListRow`s, so nothing scales with the list's length; at most `kMaxWindowRows` rows are
/// drawn. `Response::changed` reports that \p index moved, `Response::entered` a select press.
/// `Response::focused` is not set: like `list` (list.h:101-102) this widget has no `focused`
/// parameter to echo, since focus lives in the scene that owns the `ListState`.
Response enum_value(OledFrame& f, gfx::oled::Rect rect, int32_t& index, std::span<const std::string_view> names,
                    ListState& state, ScrollText& scroll_label, bool wrap = false);

/// `Toggle::drawPixelsForOled` (toggle.cpp:23-43): two rows, off then on, with the current one
/// highlighted. **`menu_rect(f)` is the rect that reproduces legacy** -- legacy's `yPixel` is the
/// same `(OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14` base-y (toggle.cpp:26) that `menu_rect`
/// returns -- and the golden is recorded with it.
///
/// Legacy draws those two rows itself, but with the same calls per row that
/// `MenuItem::drawItemsForOled` makes -- `drawString(name, kTextSpacingX, yPixel, kTextSpacingX,
/// kTextSpacingY)`, `invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel
/// + 8)` and `setupSideScroller(0, name, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, ...)` on the
/// selected one -- so this is `list` over \p rect with a two-row span, which is those calls
/// exactly. The spec's "checkbox glyph" is legacy's *submenu* rendering of a toggle
/// (`Toggle::renderSubmenuItemTypeForOled`, toggle.cpp:50-57), i.e. a `ListRow::icon` on the
/// parent menu's row, not part of this screen.
///
/// A turn does not flip the value: `Toggle::selectEncoderAction` is `setValue(offset > 0)`
/// (toggle.cpp:14-17), so a turn selects the row in its own direction and turning further that
/// way does nothing.
///
/// **A select press does not change the value either.** This screen only exists while the Toggle
/// *is* the current menu item (`Toggle::shouldEnterSubmenu()` is false, toggle.h:19, so no
/// submenu navigation can enter it), and on that branch `Toggle::selectButtonPress` returns
/// `nullptr` without touching the value (toggle.h:35-36); `SoundEditor::potentiallyEnterMenu`'s
/// null case then calls `goUpOneLevel()` (sound_editor.cpp:357, 386-389). The `toggleValue()`
/// branch (toggle.h:39-43) belongs to a Toggle rendered as a *row inside a Submenu*, with the
/// checkbox icon -- the parent menu's `list` row, not this widget. So a press reports
/// `Response::entered` and leaves `Response::changed` false, and the scene navigates, exactly as
/// it does for `enum_value` (`Selection::selectButtonPress` has the identical shape,
/// selection.h:53-57).
Response toggle(OledFrame& f, gfx::oled::Rect rect, bool& value, std::string_view off_label, std::string_view on_label,
                ScrollText& scroll_label);

} // namespace deluge::ui::oled
