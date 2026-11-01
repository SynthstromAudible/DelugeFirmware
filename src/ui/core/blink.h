#pragma once
#include "ui/core/state.h"

#include <cstdint>

namespace deluge::ui {

/// @brief Is a free-running blink lit at @p now_ms?
///
/// `OLED::performBlink`'s square wave: the phase inverts every `period_ms` from the anchor `until_ms`, dark for the
/// first period after it and lit for the next. `Blink::restart_lit` back-dates the anchor one period so the blink
/// starts lit.
/// @param blink  The blink.
/// @param now_ms The clock now.
/// @return True while lit.
bool blink_lit(const Blink& blink, uint32_t now_ms);

/// @brief Milliseconds until @p blink next flips: what the next frame has to be scheduled for.
/// @param blink  The blink.
/// @param now_ms The clock now.
/// @return The time to the next flip, in `(0, period_ms]`.
uint32_t blink_next_flip_in(const Blink& blink, uint32_t now_ms);

} // namespace deluge::ui
