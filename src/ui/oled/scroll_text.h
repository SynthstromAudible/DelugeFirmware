#pragma once
#include "gfx/oled/font.h"
#include "gfx/oled/geometry.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

/// Port of the legacy ping-pong side-scroller: `OLED::setupSideScroller` plus the scroller
/// branch of `OLED::scrollingAndBlinkingTimerEvent` (src/deluge/hid/display/oled.cpp), merged
/// into one call a widget makes every frame rather than an arm-once/tick-later pair.
///
/// If `s` fits inside `rect` at `font`'s metrics (`string_width_px(s, height) <= rect.w`), it
/// is drawn once from the start and `state.offset`/`state.dir` are reset to `0`/`+1` -- the
/// widget is not actively scrolling, the same case legacy's playback-running branch resets
/// `scroller->pos` for (`oled.cpp:975-981`) -- and no repaint is requested.
///
/// Otherwise it is drawn from `state.offset` px. `state.text_w_last` / `state.rect_w_last`
/// record the string's pixel width and `rect.w` from the last call; whenever either has
/// changed (including the very first call a fresh `ScrollText` ever sees, since both default
/// to 0), the label is treated as newly appeared, exactly like a fresh call to legacy's
/// `setupSideScroller` (`oled.cpp:913-914`): `state.offset` and `state.dir` reset to `0`/`+1`
/// and `state.next_step_ms` arms the *first* tick to a `kScrollTime` (400ms) pause -- the same
/// pause used at the ping-pong ends, not a 15/5ms step. Once armed and unchanged, while
/// `allow_motion` (legacy: the transport is not running) and `now_ms` has reached
/// `state.next_step_ms`, `state.offset` advances by one pixel in `state.dir` and
/// `state.next_step_ms` is rearmed: `kScrollStepForwardMs` (15ms) ahead while moving right,
/// `kScrollStepBackMs` (5ms) while moving left. Reaching either end of the scrollable range
/// instead pauses for `kScrollTime` and flips `state.dir` (`oled.cpp:1010-1023`). A repaint is
/// requested for `state.next_step_ms` on every call that scrolls. `allow_motion == false`
/// resets `state.offset`/`state.dir` to `0`/`+1` and requests nothing (legacy: playback running
/// freezes the label at its start and forces `sideScrollerDirection = 1`, `oled.cpp:1020-1021`).
///
/// Caveat: the change detection is by pixel width, not string identity, so two different
/// strings of exactly the same width sharing one `ScrollText` at the same rect width are
/// indistinguishable from "unchanged" and inherit each other's scroll position -- accepted,
/// since `ScrollText` stays trivially-copyable POD rather than owning a copy of the string to
/// compare byte-for-byte.
///
/// `highlight` inverts `rect` after drawing (legacy `doHighlight`, with the accessibility
/// "inversion" mode assumed on -- see `oled.cpp:995-998`).
///
/// `clear_pad_x` widens the *cleared* area only, by that many px past `rect.right()` --
/// legacy's `endX += 4` (`oled.cpp:984-992`), used for submenu rows so a shrinking scroller
/// doesn't leave stray ink where an icon is about to be drawn just past it. That padding is
/// conditioned on a global accessibility flag (`FlashStorage::accessibilityMenuHighlighting`)
/// this layer must not read (spec: `ui` may not include `notify`/`model`/`gui`/`hid`), so it is
/// exposed as an explicit, caller-supplied pixel count instead of being derived here. It widens
/// only the clear: the text's own right clip (`rect.right()`, unchanged) and the scrollable
/// range/ping-pong bounds (computed from `rect.w`, unchanged) are exactly as legacy leaves them
/// -- `drawString`'s `end_x` and `invertArea`'s span both stay at the unpadded `endX`
/// (`oled.cpp:993-994`, `996-997`), and `boxLengthPixels`/the "reached the end" check
/// (`oled.cpp:896-897`, `968`) are computed from the unpadded `endX` at setup time, so the +4
/// never affects where the scroller reverses.
Response scroll_text(OledFrame& f, gfx::oled::Rect rect, std::string_view s, gfx::oled::FontId font, ScrollText& state,
                     bool allow_motion = true, bool highlight = false, int32_t clear_pad_x = 0);

} // namespace deluge::ui::oled
