#include "fake_legacy_hid.h"

#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/control_surface.h"

#include <cstring>

namespace fakelegacy {

std::vector<ColumnSend> column_sends;
std::vector<LedWrite> led_writes;
std::vector<IndicatorWrite> indicator_writes;
std::vector<std::pair<uint8_t, std::vector<RGB>>> raw_pad_column_writes;
int control_flushes = 0;
int oled_frames_marked = 0;
uint32_t pad_output_space = 1u << 20;

} // namespace fakelegacy

// ---------------------------------------------------------------------------
// hid/display/oled.cpp's share: the main canvas and the dirty flag.
// ---------------------------------------------------------------------------
namespace deluge::hid::display {

oled_canvas::Canvas OLED::main;
bool OLED::needsSending = false;

// The fake stands in for `OLED::sendMainImage` (`hid/display/oled.cpp:335-385`) and keeps the two
// things about it a guest can observe: it does nothing when the flag is down, and it lowers the
// flag when it sends. Everything else that function does -- the screensaver swap, the console and
// popup compositing, the sysex mirror, the actual `deluge_display_blit_oled` -- belongs to legacy
// and is out of this spec's scope.
void OLED::sendMainImage() {
	if (!needsSending) {
		return;
	}
	fakelegacy::oled_frames_marked++;
	needsSending = false;
}

} // namespace deluge::hid::display

// ---------------------------------------------------------------------------
// hid/led/pad_leds.cpp's share: the pad image and the per-column send.
// ---------------------------------------------------------------------------
namespace PadLEDs {

RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth];

// Stands in for `sortLedsForCol` (`hid/led/pad_leds.cpp:260-275`). The real one reads `image`
// through `prepareColour` and pushes the pair to the control surface; the fake records the column
// it was asked for and pushes what `image` holds *at that moment*, which is what lets a spec check
// that the guest wrote the buffer BEFORE asking legacy to send it, not after.
void sortLedsForCol(int32_t x) {
	x &= 0b11111110;
	fakelegacy::column_sends.push_back(fakelegacy::ColumnSend{x});
	std::vector<RGB> colours;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		colours.push_back(image[y][x]);
	}
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		colours.push_back(image[y][x + 1]);
	}
	fakelegacy::raw_pad_column_writes.emplace_back(static_cast<uint8_t>(x >> 1), std::move(colours));
}

} // namespace PadLEDs

// ---------------------------------------------------------------------------
// hid/led/indicator_leds.cpp's share: the one entry point the guest calls.
// ---------------------------------------------------------------------------
namespace indicator_leds {

// Stands in for `setLedState` (`hid/led/indicator_leds.cpp:42-52`). The real one also stops any
// blinker on the LED and updates `ledStates[]`; neither is observable through the header, so the
// fake records the call and its two arguments, which is the mapping under test.
void setLedState(LED led, bool newState, bool allowContinuedBlinking) {
	(void)allowContinuedBlinking;
	fakelegacy::led_writes.push_back(fakelegacy::LedWrite{static_cast<uint8_t>(led), newState});
}

} // namespace indicator_leds

// ---------------------------------------------------------------------------
// The libdeluge control-surface boundary, which this suite links no BSP for.
// ---------------------------------------------------------------------------
extern "C" {

uint32_t deluge_control_pad_output_space(void) {
	return fakelegacy::pad_output_space;
}

void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count) {
	fakelegacy::IndicatorWrite w{which, {}, count};
	for (uint8_t i = 0; i < count && i < 4; i++) {
		w.levels[i] = levels[i];
	}
	fakelegacy::indicator_writes.push_back(w);
}

void deluge_control_flush(void) {
	fakelegacy::control_flushes++;
}

} // extern "C"

namespace fakelegacy {

void reset() {
	column_sends.clear();
	led_writes.clear();
	indicator_writes.clear();
	raw_pad_column_writes.clear();
	control_flushes = 0;
	oled_frames_marked = 0;
	pad_output_space = 1u << 20;
	std::memset(PadLEDs::image, 0, sizeof(PadLEDs::image));
	deluge::hid::display::OLED::main.clear();
	// `Canvas::clear()` only zeroes the bytes; it does not touch the dirty flag. Drain it through
	// the same door a spec would.
	consume_oled_frame();
	oled_frames_marked = 0;
}

bool consume_oled_frame() {
	const int before = oled_frames_marked;
	deluge::hid::display::OLED::sendMainImage();
	return oled_frames_marked != before;
}

bool oled_marked() {
	if (!consume_oled_frame()) {
		return false;
	}
	// Put it back: this query is meant to be non-destructive.
	deluge::hid::display::OLED::markChanged();
	oled_frames_marked--;
	return true;
}

} // namespace fakelegacy
