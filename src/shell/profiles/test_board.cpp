#include "test_board.h"
#include "deluge.h"
#include "libdeluge/board.h"
#include "shell/profile.h"
#include "ui/core/style.h"

namespace deluge::ui::shell {

// Test board with 3 OLEDs 128×64 (Main, Param0, Aux), same button matrix and encoders as Deluge.
// Reuses kMatrix, kEncoders, kKinds, and kLedByButton from deluge.cpp (not exposed but constexpr).

// OLED roles: Main, Param0, Aux (all 128×64).
static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Param0, SurfaceRole::Aux};
static_assert(sizeof(kRoles) / sizeof(kRoles[0]) == 3);

// Display styles: 128×64 toolkit style for all three panels.
static constexpr const Style* kStyles[] = {&kDelugeStyle64, &kDelugeStyle64, &kDelugeStyle64};
static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == 3);

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
	// Reuse the same button matrix and encoders as Deluge.
	// Copy the kMatrix and other tables from deluge.cpp by declaring them here too.
	// clang-format off
	static constexpr ButtonId kMatrix[] = {
	    // x:  0                  1               2               3                       4                    5                6                      7                      8
	    /*y0*/ ButtonId::YEnc,    ButtonId::Mod0, ButtonId::Mod4, ButtonId::AffectEntire, ButtonId::None,      ButtonId::Synth, ButtonId::Scale,       ButtonId::Learn,       ButtonId::Shift,
	    /*y1*/ ButtonId::XEnc,    ButtonId::Mod1, ButtonId::Mod5, ButtonId::Session,      ButtonId::TempoEnc,  ButtonId::Kit,   ButtonId::Load,        ButtonId::Back,        ButtonId::Triplets,
	    /*y2*/ ButtonId::ModEnc0, ButtonId::Mod2, ButtonId::Mod6, ButtonId::Clip,         ButtonId::None,      ButtonId::Midi,  ButtonId::CrossScreen, ButtonId::SyncScaling, ButtonId::Record,
	    /*y3*/ ButtonId::ModEnc1, ButtonId::Mod3, ButtonId::Mod7, ButtonId::Keyboard,     ButtonId::SelectEnc, ButtonId::Cv,    ButtonId::Save,        ButtonId::TapTempo,    ButtonId::Play,
	};
	// clang-format on
	static constexpr int8_t kLedByButton[] = {
	    -1, // None
	    -1, // AffectEntire
	    -1, // Session
	    -1, // Clip
	    -1, // Synth
	    -1, // Kit
	    -1, // Midi
	    -1, // Cv
	    -1, // Keyboard
	    -1, // Scale
	    -1, // CrossScreen
	    -1, // Back
	    -1, // Load
	    -1, // Save
	    -1, // Learn
	    -1, // TapTempo
	    -1, // SyncScaling
	    -1, // Triplets
	    35, // Play
	    26, // Record
	    8,  // Shift
	    1,  // Mod0
	    10, // Mod1
	    19, // Mod2
	    28, // Mod3
	    2,  // Mod4
	    11, // Mod5
	    20, // Mod6
	    29, // Mod7
	    -1, // SelectEnc
	    -1, // TempoEnc
	    -1, // XEnc
	    -1, // YEnc
	    -1, // ModEnc0
	    -1, // ModEnc1
	};
	static constexpr EncoderRole kEncoders[] = {
	    EncoderRole::Vertical, EncoderRole::Horizontal, EncoderRole::Tempo,
	    EncoderRole::Select,   EncoderRole::GoldUpper,  EncoderRole::GoldLower,
	};
	static constexpr EncoderKind kKinds[] = {
	    EncoderKind::Detented,     EncoderKind::Detented,     EncoderKind::Detented,
	    EncoderKind::Accelerating, EncoderKind::Accelerating, EncoderKind::Detented,
	};
	static const BoardProfile profile{
	    .button_by_matrix = kMatrix,
	    .led_by_button = kLedByButton,
	    .encoder_by_index = kEncoders,
	    .encoder_kind = kKinds,
	    .oled_role_by_index = kRoles,
	    .style_by_index = kStyles,
	    .button_cols = 9,
	    .button_rows = 4,
	    .sidebar_cols = 2,
	};
	return profile;
}

} // namespace deluge::ui::shell
