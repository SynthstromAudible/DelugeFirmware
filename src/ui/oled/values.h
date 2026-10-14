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

/// @brief Bounds an edited value is held inside, and the amount one select detent moves it.
///
/// @note `step` is the amount one select detent moves an integer (`int_value`) or a range
///       (`range_value`) -- legacy's `Integer`/`IntegerRange` step, added raw (`* coarse_step`).
///       `decimal_value` does not read this field at all: its detent moves by `10^edit_pos`
///       instead (see its own doc). There is no fine knob -- the toolkit had one, briefly, and it
///       had no legacy counterpart.
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
///
/// Ports legacy `Integer::selectEncoderAction` (`gui/menu_item/integer.cpp:25`): `f.turn()` moves
/// the value by `range.step` per detent, multiplied by 5 while `f.shift()` is held -- the select
/// encoder's own acceleration, `SoundEditor::selectEncoderAction`'s `scaledOffset`
/// (`gui/ui/sound_editor.cpp:1072`). The gold knobs are never read: legacy's `SoundEditor::
/// modEncoderAction` (`:1638`) falls through to `View::modEncoderAction` (`gui/views/view.cpp:835`),
/// which edits the mod-controllable's param instead of the menu item.
///
/// The sum is clamped to `[range.min, range.max]`; the value is never snapped to a multiple of
/// `step`.
///
/// @param f       Frame to render into and read input from.
/// @param rect    Rect the digits are laid out relative to.
/// @param value   Edited value, in/out.
/// @param range   Value's bounds and the select detent's step.
/// @param style   Unit suffix and zero-as-"OFF" presentation.
/// @param focused Whether this control currently owns the select encoder.
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
/// Ports legacy `Decimal::selectEncoderAction` (`gui/menu_item/decimal.cpp:57`): `f.turn()` moves
/// the value by `10^edit_pos` per detent, multiplied by 5 while `f.shift()` is held -- only while
/// @p focused. `range.step` plays no part here; unlike `int_value` this widget does not read it at
/// all. The gold knobs are never read, for the reason `int_value`'s doc gives.
///
/// @note The clamp applies to both ends of `[range.min, range.max]` at once, regardless of turn
///       direction -- an incoming value already outside the range gets pulled back in even on a
///       turn away from the violated bound.
///
/// @param f        Frame to render into and read input from.
/// @param rect     Rect the digits are laid out relative to (horizontally only -- see above).
/// @param value    Edited value, in/out.
/// @param range    Value's bounds. `range.step` is unused here -- see above.
/// @param places   Number of decimal places to zero-pad and place the period after.
/// @param edit_pos Digit position the cursor and select edit amount are anchored to, in/out.
/// @param blink    Cursor blink phase state, armed and advanced by this widget.
/// @param focused  Whether this control currently owns the horizontal and select encoders.
/// @param layout   Face, horizontal-anchor and row overrides, or null for the full-panel default.
///                 Passing one switches the rows from canvas-absolute to `rect`-relative -- see
///                 `DecimalLayout::text_dy`.
/// @return `Response::changed` set when @p value moved this tick; `Response::focused` echoes
///         @p focused.
/// @brief Overrides for a `decimal_value` that has to share its panel with something else.
///
/// The default -- the huge face, centred across @p rect, on the canvas-absolute rows 20 and 41 --
/// is `Integer`/`Decimal`'s own full-panel layout and is what every menu leaf wants. One page does
/// not: `PatchCableStrength::renderOLED` puts a routing diagram down the left, the number beside
/// it and a polarity switch below the number, and it uses **none** of `Decimal`'s constants --
/// its own are the 13px "Big" face, right-aligned three pixels off the panel's right edge, at
/// `textY` with the cursor at `textY + digitHeight + 1`
/// (`gui/menu_item/patch_cable_strength.cpp:172-188 renderOLED`). In the huge face the same string
/// is 18px per glyph and runs off the panel for every value of magnitude 10 or more -- most of the
/// +/-50.00 range, on the one number the user is there to edit. Hence an option rather than a
/// second widget: the editing behaviour, the cursor, the blink and the clamp are all identical.
struct DecimalLayout {
	/// @brief The digit face, in place of `Style::big_font`. `FontId::MetricBold13` is legacy's
	///        "Big" face (11px advance, 13px height -- `kTextBigSpacingX`/`kTextBigSizeY`).
	gfx::oled::FontId font;
	/// @brief Right-align the string so its last glyph's advance ends `right_pad` px before
	///        `rect.right()`, instead of centring it across `rect.w`. Legacy's own value is 3.
	int32_t right_pad;
	/// @brief The digits' top row, **relative to `rect.y`** rather than canvas-absolute.
	///
	/// The unlaid-out widget puts its digits on canvas row 20 and its cursor on canvas row 41,
	/// because those are `Decimal::drawPixelsForOled`'s own absolute constants -- and it converts
	/// them to frame-local by subtracting `f.origin.y` (see the canvas-absolute note above). A page
	/// supplying a layout is by definition **not** that page, so it also does not want that page's
	/// rows: `patch_cable_strength.cpp`'s number sits at `extraY + OLED_MAIN_TOPMOST_PIXEL + 4`,
	/// which is 15 rows above `Decimal`'s, precisely so the polarity switch fits underneath it.
	/// Inheriting row 20 from the wrong page is what pushed the port's switch above the number
	/// instead of below it.
	///
	/// Relative to `rect.y`, not absolute, so a caller whose page lays out frame-local (the
	/// routing diagram beside it does) stays frame-local and hugs the visible top on both panel
	/// geometries, instead of half the page following `f.origin` and half not.
	int32_t text_dy = 0;
	/// @brief The cursor band's top row, relative to `rect.y`. Legacy's is
	///        `textY + digitHeight + 1`, i.e. one row under the digits.
	int32_t cursor_dy = 0;
};

Response decimal_value(OledFrame& f, gfx::oled::Rect rect, int32_t& value, Range range, uint8_t places,
                       int8_t& edit_pos, Blink& blink, bool focused = true, const DecimalLayout* layout = nullptr);

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

/// @brief Which edge of a two-ended range is being edited -- the port of
///        `deluge::gui::menu_item::RangeEdit` (`gui/menu_item/range.h:24-28`).
///
/// Legacy keeps this one enum on `SoundEditor::editingRangeEdge`, a single global shared by every
/// range item because only one is ever on screen. Here it is a caller-owned in/out parameter, which
/// is what lets the scene keep it per menu depth beside the rest of the page's state.
enum class RangeEdge : uint8_t { Off = 0, Left = 1, Right = 2 };

/// @brief What a detent does to the edge that is *not* being edited when the two would cross.
///
/// Two rules, because legacy has exactly two and they are genuinely different, not two spellings of
/// one:
///
///   * `Push` -- `IntegerRange::selectEncoderAction` (`gui/menu_item/integer_range.cpp:33-68`):
///     each edge moves freely within `[min, max]` and *drags the other one along* when it would
///     pass it (`if (upper < lower) upper = lower;`). The pair can be moved past each other's old
///     positions; what it cannot do is invert.
///   * `Bound` -- `KeyRange::selectEncoderAction` (`gui/menu_item/key_range.cpp:28-58`): each edge
///     is clamped *against the other* (`lower = clamp(lower + offset, KEY_MIN, upper)`), so an edge
///     stops dead when it reaches its neighbour and the neighbour never moves.
///
/// The difference is visible on the very first detent at a coincident pair, so it is a parameter
/// rather than a simplification: with `Push`, turning the left edge up from `5-5` gives `6-6`; with
/// `Bound` it gives `5-5` and nothing moves.
enum class RangeClamp : uint8_t { Push, Bound };

/// @brief How one end of a range is spelled -- the caller's half of `Range::getText`
///        (`gui/menu_item/range.h:39-40`), which is pure virtual there for the same reason it is a
///        function pointer here.
///
/// A plain function pointer, not a callable object: both shipped spellings are pure functions of the
/// value (`intToString` for `IntegerRange`, the note-letter table for `KeyRange`), and this library
/// allocates nothing and stores no closures.
///
/// @param value   The end to spell.
/// @param scratch Storage the returned view may point into; it must outlive the `range_value` call.
/// @return The spelling. May point at static storage instead of @p scratch.
using RangeFormatFn = std::string_view (*)(int32_t value, std::span<char> scratch);

/// @brief The most characters one end of a range may spell, and therefore the scratch
///        `range_value` gives each `RangeFormatFn`.
///
/// Legacy formats both ends into one `shortStringBuffer` (`hid/display/display.h`), which is 64
/// bytes; the widest thing either shipped formatter produces is an `int32_t`'s eleven digits plus a
/// terminator. Twelve each is that, with the two ends separated so a formatter cannot run into its
/// neighbour's half.
inline constexpr size_t kRangeTextMax = 12;

/// @brief Renders and edits a two-ended range as one huge centred `lower-upper`, with the edge being
///        edited inverted.
///
/// The port of `menu_item::Range` (`gui/menu_item/range.cpp`) -- `drawPixelsForOled` (`:97-131`),
/// `horizontalEncoderAction` (`:35-84`) and, through @p clamp, the two subclasses'
/// `selectEncoderAction`s.
///
/// **Layout is legacy's monospaced arithmetic, not `measure()`'s.** `Range::drawPixelsForOled`
/// centres on `kTextHugeSpacingX * character_count` and sizes the highlight the same way, so a
/// narrow glyph does not narrow the box. `int_value` centres on `measure()` because
/// `Integer::drawPixelsForOled` calls `drawStringCentred`, which does. The two are different legacy
/// routines and they stay different here; using `text(Align::Centre)` would move these pixels.
///
/// A consequence worth expecting rather than being surprised by: glyphs *advance* proportionally
/// while the highlight is sized monospaced, so on a string of narrow digits the right-hand box sits
/// a few pixels right of the glyphs it names. That is what the device does today -- the goldens are
/// byte-identical to a replay of `Range::drawPixelsForOled` -- and it is not corrected here; a port
/// that quietly tightened it would be a redesign wearing a port's clothes.
///
///   - the digits sit in a `text_height_for(f.style.value_font)`-tall band at `rect.y + kValueY`,
///     the same band `int_value` uses, cleared before drawing;
///   - the string is `lower`, then `-` and `upper` **unless** @p edge is `Off` and the two ends are
///     equal, which is `Range::drawPixelsForOled`'s `mayShowJustOne` argument (`:102`);
///   - the highlight inverts one end plus a 1px margin above and below (`:120-123`), and nothing at
///     all when @p edge is `Off`.
///
/// @note Ports the two `selectEncoderAction`s together with `horizontalEncoderAction`: legacy edits
///       the value with the *select* encoder (`IntegerRange::selectEncoderAction`,
///       `integer_range.cpp:33`; `KeyRange::selectEncoderAction`, `key_range.cpp:28`) and picks the
///       edge with the horizontal one (`Range::horizontalEncoderAction`, `range.cpp:35`), and this
///       widget reads the same two. `f.turn()` moves the selected edge by `range.step` per detent,
///       multiplied by 5 while `f.shift()` is held, for the reason `int_value`'s doc gives; the
///       gold knobs are never read.
///
/// @note Legacy's `horizontalEncoderAction` refuses to act while shift is held (`range.cpp:37-39`)
///       and consults `mayEditRangeEdge` before selecting an edge (`:52`, `:73`). Neither is ported:
///       shift is the shell's, and `mayEditRangeEdge`'s only override in the whole firmware is
///       `MultiRange`'s (`gui/menu_item/multi_range.cpp:383`), which is a different editor this
///       widget does not serve. Adding a pair of `bool` allowances is one `if` each if it ever gains
///       a second consumer.
///
/// @param f       Frame to render into and read input from.
/// @param rect    Rect the string is laid out relative to and centred across.
/// @param lower   The low end, in/out.
/// @param upper   The high end, in/out.
/// @param range   The bounds both ends live inside, and the select detent's step.
/// @param edge    Which end is being edited, in/out. The horizontal encoder moves it.
/// @param format  How to spell one end.
/// @param clamp   Which of legacy's two crossing rules applies (`RangeClamp`).
/// @param focused Whether this control currently owns the select and horizontal encoders.
/// @return `changed` when @p lower, @p upper or @p edge actually moved this tick; `focused` echoes
///         @p focused.
Response range_value(OledFrame& f, gfx::oled::Rect rect, int32_t& lower, int32_t& upper, Range range, RangeEdge& edge,
                     RangeFormatFn format, RangeClamp clamp, bool focused = true);

} // namespace deluge::ui::oled
