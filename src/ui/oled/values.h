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

/// @brief Bounds an edited value is held inside, and the amount one detent of the primary edit
///        encoder moves it.
///
/// @note `step` is the coarse-encoder amount used by `int_value`; see its fine/coarse note for
///       how `decimal_value` uses this field differently.
struct Range {
	int32_t min, max, step = 1;
};

/// @brief Presentation options for a plain number.
struct ValueStyle {
	/// Appended to the digits, truncated to 4 characters.
	std::string_view unit = {};
	/// Draw "OFF" instead of `0`.
	bool off_at_zero = false;
};

/// @brief The largest window `enum_value` materialises on the stack.
constexpr int32_t kMaxWindowRows = 8;
static_assert(kMaxWindowRows >= kDelugeStyle48.menu_rows && kMaxWindowRows >= kDelugeStyle64.menu_rows,
              "the stack window must cover every shipped profile's menu_rows: a profile asking for "
              "more rows than the cap would have list draw the capped count and leave the rest of "
              "the panel holding stale content");

/// @brief Renders and edits a plain integer as a huge centred number.
///
/// The digits occupy a `text_height_for(f.style.value_font)`-tall band at `rect.y + kValueY`,
/// centred across `rect.w`; the band is cleared before drawing. `f.bounds()` is the rect that
/// reproduces the legacy full-panel layout, and every golden here is recorded with it.
///
/// @note Editing only happens while @p focused. Two different "focused"s meet here and compose:
///       @p focused is "am I the current control in my pane", `OledFrame::focused` is "is my pane
///       live", and the widget reads input only when both hold -- @see ui/core/frame_context.h,
///       rules 2 and 3.
///   - `f.edit(0)` (GoldUpper) moves the value by `range.step` per detent -- the coarse knob.
///   - `f.edit(1)` (GoldLower) moves it by one unit per detent -- the fine knob.
///
/// @note `decimal_value`'s coarse amount is `10^edit_pos` and `range.step` is its *fine* amount,
///       so `step` plays opposite roles in the two widgets -- they coincide only at the default
///       `step == 1`.
///
/// The sum is clamped to `[range.min, range.max]`; the value is never snapped to a multiple of
/// `step`.
///
/// @param f       Frame to render into and read input from.
/// @param rect    Rect the digits are laid out relative to.
/// @param value   Edited value, in/out.
/// @param range   Value's bounds and coarse step.
/// @param style   Unit suffix and zero-as-"OFF" presentation.
/// @param focused Whether this control currently owns the edit encoders.
/// @return `Response::changed` set when @p value actually moved this tick (a detent into a
///         clamped end changes nothing); `Response::focused` echoes @p focused.
Response int_value(OledFrame& f, gfx::oled::Rect rect, int32_t& value, Range range, ValueStyle style = {},
                   bool focused = true);

/// @brief Renders and edits a decimal value: the digits in the huge font with a half-width period
///        @p places from the right, and a blinking underline cursor under the digit at
///        @p edit_pos.
///
/// @note Reproduces the legacy placement: the digits and cursor are drawn at a canvas-absolute y,
///       NOT relative to @p rect. Every other menu renderer offsets its y by the frame's origin;
///       this one does not, so on the 48-row panel with its hidden rows the digits sit higher
///       relative to the visible area than `int_value`'s do. @p rect is honoured only
///       horizontally: the string is centred across `rect.w`, clipped at `rect.right()`, and the
///       cleared bands span `rect.x .. rect.right()`. `f.bounds()` is the rect that reproduces
///       legacy, and the goldens are recorded with it. A caller that wants a decimal inside a pane
///       needs a different widget, not a different rect.
///
/// The digits are drawn monospaced (unlike every other string in the toolkit): each advances a
/// fixed width and the period exactly half that. The digit count is `places + 1`, zero-padded,
/// with a leading `-` for negatives.
///
/// @note **Blink phase convention.** `blink.period_ms` doubles as "has this `Blink` been armed for
///       a decimal cursor yet" -- a `Blink` still holding its default period has never been
///       through here, and is armed on first use. `blink.until_ms` holds the phase *anchor*: the
///       timestamp the current blink cycle was last restarted at. The cursor is lit while
///       `((now - until_ms) / period_ms)` is odd, so it starts *dark* for one period after the
///       value changes or the widget first renders, and restarts lit on any horizontal tick (even
///       one that moves nothing because it hit a bound). One `Blink` per cursor -- sharing one
///       with a widget that wants a different period would re-anchor it every frame.
///
/// `f.horizontal()` moves @p edit_pos, one place per non-zero tick: a positive tick moves the
/// cursor right (floored at 0), a negative tick moves it left only while the next place up is
/// still reachable inside `range.max`.
///
/// `f.edit(0)` moves the value by `10^edit_pos` per detent and `f.edit(1)` by `range.step`, the
/// fine amount -- both only while @p focused.
///
/// @note The clamp applies to both ends of `[range.min, range.max]` at once, regardless of turn
///       direction -- an incoming value already outside the range gets pulled back in even on a
///       turn away from the violated bound.
///
/// @param f        Frame to render into and read input from.
/// @param rect     Rect the digits are laid out relative to (horizontally only -- see above).
/// @param value    Edited value, in/out.
/// @param range    Value's bounds and fine step.
/// @param places   Number of decimal places to zero-pad and place the period after.
/// @param edit_pos Digit position the cursor and coarse edit amount are anchored to, in/out.
/// @param blink    Cursor blink phase state, armed and advanced by this widget.
/// @param focused  Whether this control currently owns the horizontal and edit encoders.
/// @return `Response::changed` set when @p value moved this tick; `Response::focused` echoes
///         @p focused.
Response decimal_value(OledFrame& f, gfx::oled::Rect rect, int32_t& value, Range range, uint8_t places,
                       int8_t& edit_pos, Blink& blink, bool focused = true);

/// @brief Renders and edits a windowed, scrollable name list with a cursor, as one call.
///
/// `menu_rect(f)` is the rect that reproduces legacy, and the golden is recorded with it;
/// everything below is relative to it, because `list` already is. The window is `list`'s own
/// pos/tail rule; the cursor moves by `index + turn`, wrapped into `[0, n)` when @p wrap and
/// clamped to it otherwise (@p wrap defaults to false, matching legacy's default).
///
/// @note `list`'s own clamping walk is bypassed here -- it cannot wrap -- by rendering `list`
///       through an *unfocused* frame: it draws the window and the highlight and reads no input
///       at all. The press is reported by `enum_value` itself, which has claimed both the turn and
///       the select for the tick.
///
/// @param f            Frame to render into and read input from.
/// @param rect         Rect the list window is laid out in.
/// @param index        Selected index, in/out.
/// @param names        Full option list; only the visible window is materialised as `ListRow`s, so
///                     nothing scales with the list's length. At most `kMaxWindowRows` rows are
///                     drawn.
/// @param state        Published position: `cursor` is @p index, `scroll` the first visible name,
///                     `rows_last` the name count.
/// @param scroll_label Side-scroll state for a name too wide for its row.
/// @param wrap         Whether the cursor wraps at the ends instead of clamping.
/// @return `Response::changed` set when @p index moved; `Response::entered` set on a select
///         press. `Response::focused` is not set -- this widget has no `focused` parameter to
///         echo, since focus lives in the scene that owns the `ListState`.
Response enum_value(OledFrame& f, gfx::oled::Rect rect, int32_t& index, std::span<const std::string_view> names,
                    ListState& state, ScrollText& scroll_label, bool wrap = false);

/// @brief Renders and edits a boolean as a two-row off/on list, with the current row highlighted.
///
/// `menu_rect(f)` is the rect that reproduces legacy, and the golden is recorded with it. This is
/// `list` over @p rect with a two-row span; the "checkbox glyph" some callers expect belongs to a
/// toggle rendered as a *row inside* a parent menu, not to this screen.
///
/// @note A turn does not flip the value by itself -- it selects the row in the turn's direction
///       (equivalent to `setValue(offset > 0)`), and turning further that way does nothing.
///
/// @note **A select press does not change the value.** This widget only renders while the toggle
///       is the current menu item, where a press exits the item rather than flipping it -- the
///       flip-on-press behaviour belongs to the toggle-as-submenu-row case described above, which
///       is a different code path. So a press reports `Response::entered` and leaves
///       `Response::changed` false, and the scene navigates -- exactly as `enum_value` does.
///
/// @param f            Frame to render into and read input from.
/// @param rect         Rect the two-row list is laid out in.
/// @param value        Edited value, in/out.
/// @param off_label    Label for the off row.
/// @param on_label     Label for the on row.
/// @param scroll_label Side-scroll state for a label too wide for its row.
/// @return `Response::changed` set when @p value flipped; `Response::entered` set on a select
///         press.
Response toggle(OledFrame& f, gfx::oled::Rect rect, bool& value, std::string_view off_label, std::string_view on_label,
                ScrollText& scroll_label);

} // namespace deluge::ui::oled
