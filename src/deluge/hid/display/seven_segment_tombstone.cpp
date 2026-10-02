#include "hid/display/seven_segment_tombstone.h"
#include "board_layout.hpp" // kNumericDisplayLength == 4
#include <array>
#include <cstdint>

#include "libdeluge/control_surface.h" // deluge_control_flush
#include "libdeluge/display.h"         // deluge_display_write_seven_segment

namespace deluge::hid::display {

// Segment bits, matching the old SevenSegment driver:
//
//  -1-
// |   |
// 6   2
// |   |
//  -7-
// |   |
// 5   3
// |   |
//  -4-  .0
//
// 'O', 'L', 'E', 'D' — the only message this firmware can show on a 7-segment panel.
constexpr std::array<uint8_t, kNumericDisplayLength> kUpgradeMessage = {
    0x1D, // O
    0x0E, // L
    0x4F, // E
    0x3D, // D
};

[[noreturn]] void tombstoneAndHalt() {
	deluge_display_write_seven_segment(kUpgradeMessage.data(), kUpgradeMessage.size());

	// The PIC latches the segment data on flush, so we must keep flushing: a single
	// pre-halt flush can be dropped if the PIC is not yet ready to accept it.
	while (true) {
		deluge_control_flush();
	}
}

} // namespace deluge::hid::display
