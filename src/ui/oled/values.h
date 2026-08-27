#pragma once
#include "gfx/oled/geometry.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
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

/// The huge centred number of `Integer::drawPixelsForOled` (integer.cpp:59-61), which is
/// `drawInteger(kTextHugeSpacingX, kTextHugeSizeY, 18)` -- i.e. `drawStringCentred(buffer,
/// 18 + OLED_MAIN_TOPMOST_PIXEL, 18, 20)` (integer.cpp:52-57). The `+ OLED_MAIN_TOPMOST_PIXEL`
/// is the frame's origin, so the layout constant here is the frame-local `kValueY == 18`, and
/// the centring is about the frame's own width rather than the panel's.
///
/// The face is `f.style.value_font`; both shipped profiles set `MetricBold20`, whose metrics
/// are exactly the `kTextHugeSpacingX` / `kTextHugeSizeY` pair legacy hard-coded, so the pixels
/// are unchanged.
///
/// The `kTextHugeSizeY`-tall band the digits occupy is cleared first. Legacy got the same
/// effect from `renderUIsForOled` clearing the whole canvas before `drawPixelsForOled`; an
/// immediate-mode widget owns its own band instead.
///
/// Editing, only while \p focused (spec B section 3: `edit[]` changes the focused value, with no
/// modal edit state):
///   - `input.edit[0]` (GoldUpper) moves the value by `range.step` per detent -- the coarse
///     knob;
///   - `input.edit[1]` (GoldLower) moves it by one unit per detent -- the fine knob.
/// The coarse/fine split is legacy's only precedent for two edit granularities on one number,
/// `Decimal::getNumberEditSize` (decimal.cpp:49-55), which likewise pairs a coarse step with a
/// 1-unit fine one; `Integer::selectEncoderAction` itself has a single encoder and a `step` of 1
/// (integer.cpp:25-39), for which the two arms coincide. The sum is added and then clamped to
/// `[range.min, range.max]` (integer.cpp:27-36); the value is *not* snapped to a multiple of
/// `step`, because legacy never snapped. `Response::changed` reports that the value actually
/// moved -- a detent into a clamped end changes nothing and reports nothing -- and requests a
/// repaint. `Response::focused` echoes \p focused.
Response int_value(OledFrame& f, int32_t& value, Range range, ValueStyle style = {}, bool focused = true);

/// `Decimal::drawPixelsForOled` (decimal.cpp:128-164): the value's digits in the huge font with
/// a half-width period \p places from the right, and a blinking underline cursor under the digit
/// at \p edit_pos.
///
/// **Canvas-absolute y, reproduced deliberately.** Every other menu renderer offsets its y by
/// `OLED_MAIN_TOPMOST_PIXEL`; this one does not -- the digits go to a literal `20`
/// (decimal.cpp:149, 152, 161) and the cursor to a literal `41`/`42` (decimal.cpp:163), in
/// canvas rows, so on the 48-row panel with its 5 hidden rows the digits sit 5 rows *higher*
/// relative to the visible area than `int_value`'s do. The same goes for the horizontal
/// centring, which is over `OLED_MAIN_WIDTH_PIXELS` (decimal.cpp:142) rather than over the
/// frame. Those coordinates are converted back to frame-local by subtracting `f.origin`, which
/// on the root frame is `(0, visible_top)` and so reproduces the legacy canvas rows exactly;
/// inside a sub-region it still lands on canvas row 20, which is the only reading of a
/// canvas-absolute constant that means anything. The panel width is `f.canvas.geometry().width`,
/// legacy's `OLED_MAIN_WIDTH_PIXELS`.
///
/// Layout, verbatim from decimal.cpp:129-161: the digits are drawn with `use_width` set, so each
/// advances exactly `kTextHugeSpacingX` (18px) and the period exactly half that (9px) --
/// monospaced, unlike every other string in the toolkit. `length` is the width of
/// `intToString(value, buffer, places + 1)`: the digits of |value| zero-padded to `places + 1`,
/// with a leading `-` for negatives. `stringStartX` centres `18 * length + (places ? 9 : 0)`
/// over the panel.
///
/// The cursor is the `kFlashTime` blink of `OLED::setupBlink` / `performBlink`
/// (oled.cpp:832-849): a 2-row bar at canvas rows 41..42, `kTextHugeSpacingX - 2` px wide,
/// starting one pixel right of the edited digit's own start (decimal.cpp:163), drawn by
/// inverting that band. Legacy's `uiTimerManager` half-period becomes \p blink plus
/// `gate.request_repaint_after`, and legacy's `shouldBlinkImmediately` -- true only when the
/// horizontal encoder just moved the cursor (decimal.cpp:98-100) -- becomes "reset the phase to
/// lit". A full legacy redraw re-armed the timer without inverting, so the cursor starts *dark*
/// for one `kFlashTime` after the value changes or the widget first renders; that is reproduced.
///
/// `blink` is (re)armed by this widget: `Blink::period_ms` holds `kFlashTime` and
/// `Blink::until_ms` holds the phase *anchor* -- the ms timestamp the current blink cycle was
/// last restarted at -- with the cursor lit while `((now - until_ms) / period_ms)` is odd. Spec
/// B section 3 declares `Blink`'s two fields without assigning them meanings, and this is the
/// only meaning that fits legacy's free-running square wave into two words with no phase bit;
/// `period_ms != kFlashTime` doubles as "this Blink has not been armed for a decimal cursor
/// yet". Documented here rather than changing `state.h`, on the same footing as `ScrollText`'s
/// recorded deviation.
///
/// `input.horizontal` moves \p edit_pos, one place per non-zero tick, exactly as
/// `Decimal::horizontalEncoderAction` (decimal.cpp:83-101): a positive tick moves the cursor
/// right (`edit_pos--`, floored at 0), a negative tick moves it left (`edit_pos++`) only while
/// the next place up is still reachable inside `range.max` (legacy `numberEditSize * 10 <=
/// getMaxValue()`). `input.edit[0]` moves the value by `10^edit_pos` per detent -- legacy's
/// `numberEditSize` (decimal.cpp:39-42, 58) -- and `input.edit[1]` by `range.step`, the same
/// coarse/fine pair as `int_value` and for the same reason (decimal.cpp:49-55). Both only while
/// \p focused. Legacy's `scrollToGoodPos` window is not ported: `drawPixelsForOled` never reads
/// `numberScrollAmount`, so it has no effect on any pixel.
Response decimal_value(OledFrame& f, int32_t& value, Range range, uint8_t places, int8_t& edit_pos, Blink& blink,
                       bool focused = true);

/// The largest window `enum_value` materialises on the stack -- comfortably above both shipped
/// profiles' `style.menu_rows` (3 and 5), so it never binds on a real panel.
constexpr int32_t kMaxWindowRows = 8;

/// The windowed name list of `Selection::drawPixelsForOled` (selection.cpp:8-30), as one call:
/// `list` over `menu_rect(f)` with one row per name and the cursor on \p index.
///
/// The window is `list`'s (which is `Selection`'s own pos/tail rule, already ported); the cursor
/// movement is `Enumeration::selectEncoderAction` (enumeration.cpp:18-36): `index + turn`,
/// wrapped into `[0, n)` when \p wrap and clamped to it otherwise. `Enumeration::wrapAround`
/// returns false by default (enumeration.cpp:12-16), which is why \p wrap defaults to false.
/// `list`'s own clamping walk is bypassed -- it is `Submenu`'s, not `Enumeration`'s, and cannot
/// wrap -- by handing `list` a frame whose `input.turn` is zeroed.
///
/// \p state carries the widget's published position: `cursor` is \p index, `scroll` the first
/// visible name, `rows_last` the name count. Only the visible slice of \p names is materialised
/// as `ListRow`s, so nothing scales with the list's length; at most `kMaxWindowRows` rows are
/// drawn, which neither shipped profile's `style.menu_rows` (3 and 5) comes close to.
/// `Response::changed` reports that \p index moved, `Response::entered` a select press.
Response enum_value(OledFrame& f, int32_t& index, std::span<const std::string_view> names, ListState& state,
                    ScrollText& scroll_label, bool wrap = false);

/// `Toggle::drawPixelsForOled` (toggle.cpp:23-43): two rows, off then on, at the legacy menu
/// base-y, with the current one highlighted.
///
/// Legacy draws those two rows itself, but with the same four calls per row that
/// `MenuItem::drawItemsForOled` makes -- `drawString(name, kTextSpacingX, yPixel, kTextSpacingX,
/// kTextSpacingY)`, `invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel
/// + 8)` and `setupSideScroller(0, name, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, ...)` on the
/// selected one -- so this is `list` over `menu_rect(f)` with a two-row span, which is those
/// calls exactly. The spec's "checkbox glyph" is legacy's *submenu* rendering of a toggle
/// (`Toggle::renderSubmenuItemTypeForOled`, toggle.cpp:50-57), i.e. a `ListRow::icon` on the
/// parent menu's row, not part of this screen.
///
/// A turn does not flip the value: `Toggle::selectEncoderAction` is `setValue(offset > 0)`
/// (toggle.cpp:14-17), so a turn selects the row in its own direction and turning further in
/// that direction does nothing. A select press does flip it -- `Toggle::selectButtonPress` calls
/// `toggleValue()` (toggle.h:33-44) -- and reports `Response::changed`, but never
/// `Response::entered`, because `Toggle::shouldEnterSubmenu` is false (toggle.h:18-19). The
/// brief's "turn or select toggles" is resolved to legacy's split on both counts.
Response toggle(OledFrame& f, bool& value, std::string_view off_label, std::string_view on_label,
                ScrollText& scroll_label);

} // namespace deluge::ui::oled
