#pragma once
#include "gfx/grid/rgb.h"
#include "ui/core/led_frame.h"

#include <cstdint>
#include <utility>
#include <vector>

/// @brief Test doubles for the legacy HID globals `GuestSurfaceSet` writes through.
///
/// The host spec suite deliberately never builds legacy's `deluge_app` closure (see the long note
/// in `tests/spec/CMakeLists.txt`), so `hid/display/oled.cpp`, `hid/led/pad_leds.cpp` and
/// `hid/led/indicator_leds.cpp` are not linked here. What IS linked -- and what the guest actually
/// talks to -- is the handful of symbols those files define, so `fake_legacy_hid.cpp` defines those
/// symbols itself: the real `OLED::main` and `PadLEDs::image` buffer types, at their real sizes,
/// with recorders standing in for the three functions that would drive hardware.
///
/// That makes the buffers a genuine sentinel: a spec writes colours through the shell's surface and
/// reads them back out of the legacy buffer, which nothing in the shell can see or touch.
namespace fakelegacy {

/// @brief One recorded `PadLEDs::sortLedsForCol` call.
struct ColumnSend {
	int32_t x; ///< The column index legacy was asked to send (always even).
};

/// @brief One recorded `indicator_leds::setLedState` call.
struct LedWrite {
	uint8_t led; ///< `indicator_leds::LED`'s underlying value.
	bool on;
};

/// @brief One recorded `deluge_control_set_indicator` call.
struct IndicatorWrite {
	uint8_t which;
	uint8_t levels[4];
	uint8_t count;
};

/// @name Recorders
/// @{
extern std::vector<ColumnSend> column_sends;
extern std::vector<LedWrite> led_writes;
extern std::vector<IndicatorWrite> indicator_writes;
extern std::vector<std::pair<uint8_t, std::vector<RGB>>> raw_pad_column_writes;
extern int control_flushes;
/// How many times a fake `OLED::sendMainImage()` found legacy's dirty flag raised.
extern int oled_frames_marked;
/// @}

/// @brief What `deluge_control_pad_output_space()` reports. Large by default: no back-pressure.
extern uint32_t pad_output_space;

/// @brief Clear every recorder and every legacy buffer, and lower the OLED dirty flag.
void reset();

/// @brief Run the fake `OLED::sendMainImage()` and report whether it found a frame to send.
///
/// This is how a spec observes `OLED::markChanged()`: `needsSending` is a private static of `OLED`,
/// so nothing outside the class may read it -- but `sendMainImage()` is a declared static member of
/// `OLED`, and the fake's *definition* of it therefore may. It consumes the flag exactly as the real
/// one does (`hid/display/oled.cpp:336-338`, `:384`).
/// @return True if the dirty flag was raised (and it is now lowered).
bool consume_oled_frame();

/// @brief Is the OLED dirty flag currently raised, without consuming it?
bool oled_marked();

} // namespace fakelegacy
