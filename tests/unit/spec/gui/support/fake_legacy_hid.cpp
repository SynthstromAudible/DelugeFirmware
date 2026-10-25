#include "fake_legacy_hid.h"

#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"

#include <cstring>

namespace fakelegacy {

std::vector<ColumnSend> column_sends;
std::vector<LedWrite> led_writes;
std::vector<IndicatorWrite> indicator_writes;
std::vector<std::pair<uint8_t, std::vector<RGB>>> raw_pad_column_writes;
int control_flushes = 0;
std::vector<PadFlash> pad_flashes;
int greyout_reassessments = 0;
int oled_frames_marked = 0;
uint32_t pad_output_space = 1u << 20;
bool led_blinking[64]{};

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
// Defined but never written by anything under test: `GuestSurfaceSet::send_columns` and
// `ShellHostUI::renderMainPads` both leave legacy's occupancy map alone, and a spec can only assert
// that if the array exists to be looked at.
uint8_t occupancyMask[kDisplayHeight][kDisplayWidth + kSideBarWidth];

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

// Stands in for `reassessGreyout` (`hid/led/pad_leds.cpp`): counted, since the greyout it would
// re-read is legacy's own query of the open UIs.
void reassessGreyout(bool doInstantly) {
	(void)doInstantly;
	fakelegacy::greyout_reassessments++;
}

} // namespace PadLEDs

// ---------------------------------------------------------------------------
// hid/led/indicator_leds.cpp's share: the one entry point the guest calls.
// ---------------------------------------------------------------------------
namespace indicator_leds {

bool ledStates[NUM_LED_COLS * NUM_LED_ROWS];

// Stands in for `setLedState` (`hid/led/indicator_leds.cpp:42-52`): records the call and keeps
// `ledStates[]` as the real one does. The real one also stops any blinker on the LED.
void setLedState(LED led, bool newState, bool allowContinuedBlinking) {
	(void)allowContinuedBlinking;
	fakelegacy::led_writes.push_back(fakelegacy::LedWrite{static_cast<uint8_t>(led), newState});
	ledStates[static_cast<uint8_t>(led)] = newState;
	fakelegacy::led_blinking[static_cast<uint8_t>(led)] = false;
}

// Stands in for `getLedBlinkerIndex` (`hid/led/indicator_leds.cpp`): 255 means no blinker.
uint8_t getLedBlinkerIndex(LED led) {
	return fakelegacy::led_blinking[static_cast<uint8_t>(led)] ? 0 : 255;
}

} // namespace indicator_leds

// ---------------------------------------------------------------------------
// The libdeluge control-surface boundary, which this suite links no BSP for.
// ---------------------------------------------------------------------------
extern "C" {

uint32_t deluge_control_pad_output_space(void) DELUGE_NOEXCEPT {
	return fakelegacy::pad_output_space;
}

void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count) DELUGE_NOEXCEPT {
	fakelegacy::IndicatorWrite w{which, {}, count};
	for (uint8_t i = 0; i < count && i < 4; i++) {
		w.levels[i] = levels[i];
	}
	fakelegacy::indicator_writes.push_back(w);
}

void deluge_control_flash_pad(uint8_t idx) DELUGE_NOEXCEPT {
	fakelegacy::pad_flashes.push_back(fakelegacy::PadFlash{idx, 0});
}

void deluge_control_flash_pad_colour(uint8_t idx, int32_t colour_idx) DELUGE_NOEXCEPT {
	fakelegacy::pad_flashes.push_back(fakelegacy::PadFlash{idx, colour_idx});
}

void deluge_control_flush(void) DELUGE_NOEXCEPT {
	fakelegacy::control_flushes++;
}

// The rest of `shell::BoardSurfaceSet`'s boundary, so its flash sink can be driven here. Nothing
// in this suite asserts on these.
void deluge_control_set_pad_columns(uint8_t /*idx*/, const DelugeColour* /*colours*/,
                                    uint8_t /*count*/) DELUGE_NOEXCEPT {
}

void deluge_control_set_led(uint8_t /*led*/, bool /*on*/) DELUGE_NOEXCEPT {
}

DelugeStatus deluge_display_blit_oled_n(uint8_t /*index*/, const uint8_t* /*pixels*/, uint16_t /*width*/,
                                        uint16_t /*height*/) DELUGE_NOEXCEPT {
	return DELUGE_OK;
}

bool deluge_display_busy_n(uint8_t /*index*/) DELUGE_NOEXCEPT {
	return false;
}

} // extern "C"

namespace fakelegacy {

void reset() {
	column_sends.clear();
	led_writes.clear();
	pad_flashes.clear();
	greyout_reassessments = 0;
	std::memset(led_blinking, 0, sizeof(led_blinking));
	std::memset(indicator_leds::ledStates, 0, sizeof(indicator_leds::ledStates));
	indicator_writes.clear();
	raw_pad_column_writes.clear();
	control_flushes = 0;
	oled_frames_marked = 0;
	pad_output_space = 1u << 20;
	std::memset(PadLEDs::image, 0, sizeof(PadLEDs::image));
	std::memset(PadLEDs::occupancyMask, 0, sizeof(PadLEDs::occupancyMask));
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
