#pragma once
#include "libdeluge/board.h"
#include "shell/profile.h"
#include "ui/core/input.h"
#include <cstdint>
#include <span>

namespace deluge::ui::shell {

/// The real Deluge board literal: 18×8 pads (16+2 sidebar columns), 4×9 buttons, 6 encoders, 1 OLED 128×48
/// visible_top 5.
extern const DelugeBoard kDelugeBoard;

/// Deluge button matrix: 4 rows × 9 columns, indexed [y * 9 + x].
extern const std::span<const ButtonId> kDelugeButtonMatrix;

/// Deluge LED indices by ButtonId: x + y*9 for each button with an LED (e.g., Play at (8,3) → 35),
/// -1 for encoder buttons (SelectEnc, TempoEnc, XEnc, YEnc, ModEnc0, ModEnc1).
/// Contract: LedId values = ButtonId values per shell/leds.h.
extern const std::span<const int8_t> kDelugeLedByButton;

/// Deluge encoder order: libdeluge index → EncoderRole (0=Vertical, 1=Horizontal, 2=Tempo, 3=Select, 4=GoldUpper,
/// 5=GoldLower).
extern const std::span<const EncoderRole> kDelugeEncoders;

/// Deluge encoder kinds: EncoderRole → EncoderKind (Detented or Accelerating).
extern const std::span<const EncoderKind> kDelugeEncoderKinds;

/// Deluge OLED roles: display index → SurfaceRole (one Main panel).
extern const std::span<const SurfaceRole> kDelugeOledRoles;

/// Deluge OLED styles: display index → Style* (one 48-row style).
extern const std::span<const Style* const> kDelugeOledStyles;

/// The board profile for the Deluge: button matrix, LED map, encoders, styles, and shortcuts.
/// @return The profile, viewing static storage.
const BoardProfile& deluge_profile();

} // namespace deluge::ui::shell
