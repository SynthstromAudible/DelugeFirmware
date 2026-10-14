#include "test_board.h"
#include "deluge.h"
#include "libdeluge/board.h"
#include "shell/profile.h"
#include "ui/core/style.h"

namespace deluge::ui::shell {

// Test board with 3 OLEDs 128×64 (Main, Param0, Aux), same button matrix and encoders as Deluge.
// Reuses tables from deluge.cpp: kDelugeButtonMatrix, kDelugeLedByButton, kDelugeEncoders, kDelugeEncoderKinds.

// OLED roles: Main, Param0, Aux (all 128×64).
static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Param0, SurfaceRole::Aux};
static_assert(sizeof(kRoles) / sizeof(kRoles[0]) == 3);

// Display styles: 128×64 toolkit style for all three panels.
static constexpr const Style* kStyles[] = {&kDelugeStyle64, &kDelugeStyle64, &kDelugeStyle64};
static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == 3);

// Export test-board-specific spans.
const std::span<const SurfaceRole> kTestBoardOledRoles = kRoles;
const std::span<const Style* const> kTestBoardOledStyles = kStyles;

// Test board descriptor: same as Deluge for pads/buttons/encoders, but 3 OLEDs 128×64.
static constexpr DelugeDisplayDesc kDisplays[] = {
    {128, 64, 0}, // Main
    {128, 64, 0}, // Param0
    {128, 64, 0}, // Aux
};
extern const DelugeBoard kTestBoard = {
    .name = "Test board (3 OLEDs, 128x64)",
    .pad_grid_width = 16,
    .pad_grid_height = 8,
    .button_rows = 4,
    .button_cols = 9,
    .encoder_count = 6,
    .cv_channels = 2,
    .gate_channels = 4,
    .display_kind = DELUGE_DISPLAY_OLED,
    .display_width = 128,
    .display_height = 64,
    .display_count = 3,
    .displays = kDisplays,
    .audio_sample_rate_hz = 44100,
    .audio_in_channels = 2,
    .audio_out_channels = 2,
};

const BoardProfile& test_board_profile() {
	static const BoardProfile profile{
	    .button_by_matrix = kDelugeButtonMatrix,
	    .led_by_button = kDelugeLedByButton,
	    .encoder_by_index = kDelugeEncoders,
	    .encoder_kind = kDelugeEncoderKinds,
	    .oled_role_by_index = kRoles,
	    .style_by_index = kStyles,
	    .button_cols = 9,
	    .button_rows = 4,
	    .sidebar_cols = 2,
	};
	return profile;
}

} // namespace deluge::ui::shell
