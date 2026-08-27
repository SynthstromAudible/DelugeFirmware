#pragma once
#include "gfx/oled/font.h"
#include "gfx/oled/geometry.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <string_view>

namespace deluge::ui::oled {

/// Port of the legacy ping-pong side-scroller: `OLED::setupSideScroller` plus the scroller
/// branch of `OLED::scrollingAndBlinkingTimerEvent` (src/deluge/hid/display/oled.cpp), merged
/// into one call a widget makes every frame rather than an arm-once/tick-later pair.
///
/// If `s` fits inside `rect` at `font`'s metrics (`string_width_px(s, height) <= rect.w`), it
/// is drawn once and `state` is left untouched -- no repaint is requested.
///
/// Otherwise it is drawn from `state.offset` px. While `allow_motion` (legacy: the transport is
/// not running) and `now_ms` has reached `state.next_step_ms`, `state.offset` advances by one
/// pixel in `state.dir` and `state.next_step_ms` is rearmed: `kScrollStepForwardMs` (15ms)
/// ahead while moving right, `kScrollStepBackMs` (5ms) while moving left. Reaching either end
/// of the scrollable range instead pauses for `kScrollTime` (400ms) and flips `state.dir`.
/// `state.next_step_ms == 0` is the "never armed" sentinel a freshly constructed `ScrollText`
/// starts in (legacy: `setupSideScroller` schedules the first tick without moving the label);
/// the first call that finds the string too wide arms `state.next_step_ms` without stepping,
/// exactly mirroring that split. A repaint is requested for `state.next_step_ms` on every call
/// that scrolls. `allow_motion == false` resets `state.offset` to 0 and requests nothing
/// (legacy: playback running freezes the label at its start, not mid-scroll).
///
/// `highlight` inverts `rect` after drawing (legacy `doHighlight`, with inversion enabled).
Response scroll_text(OledFrame& f, gfx::oled::Rect rect, std::string_view s, gfx::oled::FontId font, ScrollText& state,
                     bool allow_motion = true, bool highlight = false);

} // namespace deluge::ui::oled
