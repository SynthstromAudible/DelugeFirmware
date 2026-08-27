#pragma once
#include "gfx/oled/font.h"
#include <cstdint>

namespace deluge::ui {
using gfx::oled::kTextBigSizeY;
using gfx::oled::kTextHugeSizeY;
using gfx::oled::kTextHugeSpacingX;
using gfx::oled::kTextSpacingX;
using gfx::oled::kTextSpacingY;
using gfx::oled::kTextTitleSizeY;
using gfx::oled::kTextTitleSpacingX;

// Text metrics the gfx font tables do not carry, reproduced from the legacy
// definitions_cxx.hpp so the FontId tables in ui/oled/text.cpp can name every arm rather than
// spelling a number.
constexpr int32_t kTextSmallSpacingX = 4; // definitions_cxx.hpp:967, the 5px font's textWidth
constexpr int32_t kTextSmallSizeY = 5;    // definitions_cxx.hpp:968, its textHeight
constexpr int32_t kTextBigSpacingX = 11;  // definitions_cxx.hpp:979, the 13px font's textWidth

constexpr int32_t kSubmenuIconSpacingX = 7;
constexpr int32_t kScrollTime = 400;         // ms pause at each end of a scrolling label
constexpr int32_t kScrollStepForwardMs = 15; // legacy: 15 ms per pixel forwards
constexpr int32_t kScrollStepBackMs = 5;     // 5 ms per pixel back
constexpr int32_t kFlashTime = 110;          // blink half-period
constexpr int32_t kHorizontalSlotYOffset = 2;
constexpr int32_t kMenuBaseY48 = 14; // list top for a 48-row panel (+ visible_top)
constexpr int32_t kMenuBaseY64 = 15;
constexpr int32_t kTitleY = 1;           // title baseline offset (+ visible_top)
constexpr int32_t kTitleSeparatorY = 12; // (+ visible_top)
constexpr int32_t kPopupMargin = 12;     // legacy doubleMargin
constexpr int32_t kPopupMaxCharsPerLine = 19;
constexpr int32_t kValueY = 18; // huge-font value baseline (+ visible_top)
constexpr int32_t kHorizontalBoxHeight = 25;
} // namespace deluge::ui
