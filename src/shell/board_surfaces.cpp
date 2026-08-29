#include "shell/board_surfaces.h"

#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"

#include <cstddef>
#include <type_traits>

namespace deluge::ui::shell {

// send_columns hands the whole ColumnPair straight to libdeluge rather than copying colour by
// colour, so the two colour types must be layout-identical -- same size, same alignment, same
// channel order. A silent mismatch would swap red and blue on every pad.
static_assert(std::is_standard_layout_v<RGB>);
static_assert(std::is_standard_layout_v<DelugeColour>);
static_assert(sizeof(RGB) == sizeof(DelugeColour));
static_assert(alignof(RGB) == alignof(DelugeColour));
static_assert(offsetof(RGB, r) == offsetof(DelugeColour, r));
static_assert(offsetof(RGB, g) == offsetof(DelugeColour, g));
static_assert(offsetof(RGB, b) == offsetof(DelugeColour, b));
static_assert(sizeof(ColumnPair) == kColumnPairLeds * sizeof(DelugeColour));

BoardSurfaceSet::BoardSurfaceSet(const DelugeBoard& board, const BoardProfile& profile) {
	build(board, profile);
}

bool BoardSurfaceSet::oled_busy(uint8_t index) {
	return deluge_display_busy_n(index);
}

void BoardSurfaceSet::blit(uint8_t index, std::span<const uint8_t> bytes, uint16_t w, uint16_t h) {
	deluge_display_blit_oled_n(index, bytes.data(), w, h);
}

uint32_t BoardSurfaceSet::pad_output_space() {
	return deluge_control_pad_output_space();
}

void BoardSurfaceSet::send_columns(uint8_t pair_index, const ColumnPair& pair) {
	deluge_control_set_pad_columns(pair_index, reinterpret_cast<const DelugeColour*>(pair.colours), kColumnPairLeds);
}

void BoardSurfaceSet::set_led(uint8_t board_led, bool on) {
	deluge_control_set_led(board_led, on);
}

void BoardSurfaceSet::set_indicator(uint8_t which, const KnobIndicator& indicator) {
	// `which` passes straight through: it is an `IndicatorId` value, and that enum is numbered
	// `{Lower = 0, Upper = 1}` precisely so it matches the `whichKnob` (= mod encoder number) the
	// boundary expects. See the ordering note on `IndicatorId` (`ui/core/led_frame.h`).
	deluge_control_set_indicator(which, indicator.levels, 4);
}

void BoardSurfaceSet::flush() {
	deluge_control_flush();
}

} // namespace deluge::ui::shell
