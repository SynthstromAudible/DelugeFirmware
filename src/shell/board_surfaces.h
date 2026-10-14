#pragma once
#include "shell/surfaces.h"

namespace deluge::ui::shell {

/// @brief The `SurfaceSet` that talks to real hardware: every sink forwards to libdeluge.
///
/// This is the only shell class bound to `libdeluge/display.h` and `libdeluge/control_surface.h`;
/// everything else in the shell sees the `SurfaceSet` virtuals.
class BoardSurfaceSet final : public SurfaceSet {
public:
	/// @brief Build the surfaces for \p board as described by \p profile.
	/// @param board Board capability descriptor, normally `*deluge_board()`.
	/// @param profile The matching board profile.
	BoardSurfaceSet(const DelugeBoard& board, const BoardProfile& profile);

	bool oled_busy(uint8_t index) override;
	void blit(uint8_t index, std::span<const uint8_t> bytes, uint16_t w, uint16_t h) override;
	uint32_t pad_output_space() override;
	void send_columns(uint8_t pair_index, const ColumnPair& pair) override;
	void set_led(uint8_t board_led, bool on) override;
	void set_indicator(uint8_t which, const KnobIndicator& indicator) override;
	void flush() override;
};

} // namespace deluge::ui::shell
