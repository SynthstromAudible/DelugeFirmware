#pragma once
#include "gfx/oled/font.h"
#include "gfx/oled/geometry.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

/// @brief Ping-pong side-scroller: draws @p s within @p rect, scrolling it if it doesn't fit.
///
/// Merges an arm-once/tick-later pair into one call a widget makes every frame.
///
/// @note Ports legacy `OLED::setupSideScroller` and the scroller branch of
///       `OLED::scrollingAndBlinkingTimerEvent`.
///
/// If @p s fits inside @p rect at @p font's metrics (`string_width_px(s, height) <= rect.w`), it
/// is drawn once from the start, `state.offset`/`state.dir` reset to `0`/`+1` (the widget is not
/// actively scrolling), and no repaint is requested.
///
/// Otherwise it is drawn from `state.offset` px.
///
/// @note **Change detection.** `state.text_w_last` / `state.rect_w_last` record the string's
///       pixel width and @p rect's width from the last call. Whenever either has changed --
///       including the very first call a fresh `ScrollText` ever sees, since both default to 0
///       -- the label is treated as newly appeared: `state.offset`/`state.dir` reset to `0`/`+1`
///       and `state.next_step_ms` arms the *first* tick to a `kScrollTime` (400ms) pause, the
///       same pause used at the ping-pong ends, not an immediate step.
///
/// @note **Stepping.** Once armed and unchanged, while @p allow_motion and `now_ms` has reached
///       `state.next_step_ms`, `state.offset` advances by one pixel in `state.dir` and
///       `state.next_step_ms` is rearmed: `kScrollStepForwardMs` (15ms) ahead while moving right,
///       `kScrollStepBackMs` (5ms) while moving left. Reaching either end of the scrollable range
///       instead pauses for `kScrollTime` and flips `state.dir`. A repaint is requested for
///       `state.next_step_ms` on every call that scrolls. @p allow_motion == false resets
///       `state.offset`/`state.dir` to `0`/`+1` and requests nothing.
///
/// @note Change detection is by pixel width, not string identity, so two different strings of
///       exactly the same width sharing one `ScrollText` at the same rect width are
///       indistinguishable from "unchanged" and inherit each other's scroll position -- accepted,
///       since `ScrollText` stays trivially-copyable POD rather than owning a copy of the string
///       to compare byte-for-byte.
///
/// @p highlight inverts @p rect after drawing, with the accessibility "inversion" mode assumed
/// on.
///
/// @note **`clear_pad_x`** widens the *cleared* area only, by that many px past `rect.right()`
///       -- used for submenu rows so a shrinking scroller doesn't leave stray ink where an icon
///       is about to be drawn just past it. It widens only the clear: the text's own right clip
///       (`rect.right()`, unchanged) and the scrollable range/ping-pong bounds (computed from
///       `rect.w`, unchanged) are unaffected -- the +4 never affects where the scroller
///       reverses. This padding is conditioned in legacy on a global accessibility flag that
///       this layer must not read, so it is exposed as an explicit, caller-supplied pixel count
///       instead of being derived here.
///
/// @param f             Frame to draw into and to time against.
/// @param rect          Box the label is drawn and clipped into.
/// @param s             The label text.
/// @param font          Font to measure and draw with.
/// @param state         Persistent scroll position/timing state.
/// @param allow_motion  Whether the scroller may advance this call.
/// @param highlight     Whether to invert @p rect after drawing.
/// @param clear_pad_x   Extra px to widen the cleared (not drawn or clipped) area by.
/// @return An empty Response; scroll_text never claims input or reports a change.
Response scroll_text(OledFrame& f, gfx::oled::Rect rect, std::string_view s, gfx::oled::FontId font, ScrollText& state,
                     bool allow_motion = true, bool highlight = false, int32_t clear_pad_x = 0);

} // namespace deluge::ui::oled
