#pragma once

namespace deluge::hid::display {

/// Displays a static "OLED" message on the 7-segment panel and halts forever.
///
/// This firmware does not support 7-segment hardware. This exists solely so a user who
/// flashes it onto a 7SEG unit sees why it stopped, rather than concluding they bricked
/// the device. It is not a display driver — no layers, popups, blinking, scrolling, or
/// localization. Do not grow it.
[[noreturn]] void tombstoneAndHalt();

} // namespace deluge::hid::display
