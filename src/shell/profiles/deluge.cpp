#include "deluge.h"
#include "libdeluge/board.h"
#include "shell/profile.h"
#include "ui/core/input.h"
#include "ui/core/style.h"

namespace deluge::ui::shell {

// Button matrix: 4 rows × 9 columns, row-major order (y * 9 + x).
// Coordinates from definitions_cxx.hpp:59-84 and button.h:20-21.
// clang-format off
static constexpr ButtonId kMatrix[] = {
    // x:  0                  1               2               3                       4                    5                6                      7                      8
    /*y0*/ ButtonId::YEnc,    ButtonId::Mod0, ButtonId::Mod4, ButtonId::AffectEntire, ButtonId::None,      ButtonId::Synth, ButtonId::Scale,       ButtonId::Learn,       ButtonId::Shift,
    /*y1*/ ButtonId::XEnc,    ButtonId::Mod1, ButtonId::Mod5, ButtonId::Session,      ButtonId::TempoEnc,  ButtonId::Kit,   ButtonId::Load,        ButtonId::Back,        ButtonId::Triplets,
    /*y2*/ ButtonId::ModEnc0, ButtonId::Mod2, ButtonId::Mod6, ButtonId::Clip,         ButtonId::None,      ButtonId::Midi,  ButtonId::CrossScreen, ButtonId::SyncScaling, ButtonId::Record,
    /*y3*/ ButtonId::ModEnc1, ButtonId::Mod3, ButtonId::Mod7, ButtonId::Keyboard,     ButtonId::SelectEnc, ButtonId::Cv,    ButtonId::Save,        ButtonId::TapTempo,    ButtonId::Play,
};
// clang-format on
static_assert(sizeof(kMatrix) / sizeof(kMatrix[0]) == 9 * 4);

// LED indices by ButtonId: x + y*9 for buttons with LEDs (from indicator_leds.h:37-56), -1 for encoder buttons.
// All values from legacy LED enum: indicator_leds.h:36-66.
static constexpr int8_t kLedByButton[] = {
    -1, // None
    3,  // AffectEntire (3, 0) -> 3; indicator_leds.h:37
    12, // Session (3, 1) -> 12; indicator_leds.h:38
    21, // Clip (3, 2) -> 21; indicator_leds.h:39
    5,  // Synth (5, 0) -> 5; indicator_leds.h:40
    14, // Kit (5, 1) -> 14; indicator_leds.h:41
    23, // Midi (5, 2) -> 23; indicator_leds.h:42
    32, // Cv (5, 3) -> 32; indicator_leds.h:43
    30, // Keyboard (3, 3) -> 30; indicator_leds.h:44
    6,  // Scale (6, 0) -> 6; indicator_leds.h:45
    24, // CrossScreen (6, 2) -> 24; indicator_leds.h:46
    16, // Back (7, 1) -> 16; indicator_leds.h:47
    15, // Load (6, 1) -> 15; indicator_leds.h:48
    33, // Save (6, 3) -> 33; indicator_leds.h:49
    7,  // Learn (7, 0) -> 7; indicator_leds.h:50
    34, // TapTempo (7, 3) -> 34; indicator_leds.h:51
    25, // SyncScaling (7, 2) -> 25; indicator_leds.h:52
    17, // Triplets (8, 1) -> 17; indicator_leds.h:53
    35, // Play (8, 3) -> 35; indicator_leds.h:54
    26, // Record (8, 2) -> 26; indicator_leds.h:55
    8,  // Shift (8, 0) -> 8; indicator_leds.h:56
    1,  // Mod0 (1, 0) -> 1; indicator_leds.h:58
    10, // Mod1 (1, 1) -> 10; indicator_leds.h:59
    19, // Mod2 (1, 2) -> 19; indicator_leds.h:60
    28, // Mod3 (1, 3) -> 28; indicator_leds.h:61
    2,  // Mod4 (2, 0) -> 2; indicator_leds.h:62
    11, // Mod5 (2, 1) -> 11; indicator_leds.h:63
    20, // Mod6 (2, 2) -> 20; indicator_leds.h:64
    29, // Mod7 (2, 3) -> 29; indicator_leds.h:65
    -1, // SelectEnc
    -1, // TempoEnc
    -1, // XEnc
    -1, // YEnc
    -1, // ModEnc0
    -1, // ModEnc1
};
static_assert(sizeof(kLedByButton) / sizeof(kLedByButton[0]) == static_cast<size_t>(ButtonId::Count));
static_assert(kLedByButton[static_cast<size_t>(ButtonId::Play)] == 35);
static_assert(kLedByButton[static_cast<size_t>(ButtonId::Record)] == 26);
static_assert(kLedByButton[static_cast<size_t>(ButtonId::AffectEntire)] == 3);
static_assert(kLedByButton[static_cast<size_t>(ButtonId::Session)] == 12);

// Encoder order from encoder_input.cpp:56-63: 0=scrollY 1=scrollX 2=tempo 3=select 4=mod1(upper) 5=mod0(lower).
// 4=mod1=upper gold; 5=mod0=lower gold (encoders.h:80-81).
static constexpr EncoderRole kEncoders[] = {
    EncoderRole::Vertical,   // 0
    EncoderRole::Horizontal, // 1
    EncoderRole::Tempo,      // 2
    EncoderRole::Select,     // 3
    EncoderRole::GoldUpper,  // 4
    EncoderRole::GoldLower,  // 5
};
static_assert(sizeof(kEncoders) / sizeof(kEncoders[0]) == 6);

// Encoder kinds indexed by EncoderRole: 4 detented (Select, Horizontal, Vertical, Tempo), 2 accelerating (GoldUpper,
// GoldLower).
static constexpr EncoderKind kKinds[] = {
    EncoderKind::Detented,     // Select
    EncoderKind::Detented,     // Horizontal
    EncoderKind::Detented,     // Vertical
    EncoderKind::Accelerating, // GoldUpper
    EncoderKind::Accelerating, // GoldLower
    EncoderKind::Detented,     // Tempo
};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == static_cast<size_t>(EncoderRole::Count));

// OLED roles: Deluge has one main OLED.
static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main};
static_assert(sizeof(kRoles) / sizeof(kRoles[0]) == 1);

// Display styles: Deluge has one 128×48 OLED.
static constexpr const Style* kStyles[] = {&kDelugeStyle48};
static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == 1);

// Export table spans for use by test_board and other profiles.
const std::span<const ButtonId> kDelugeButtonMatrix = kMatrix;
const std::span<const int8_t> kDelugeLedByButton = kLedByButton;
const std::span<const EncoderRole> kDelugeEncoders = kEncoders;
const std::span<const EncoderKind> kDelugeEncoderKinds = kKinds;
const std::span<const SurfaceRole> kDelugeOledRoles = kRoles;
const std::span<const Style* const> kDelugeOledStyles = kStyles;

// Deluge board descriptor: 16 main pad columns + 2 sidebar = 18 total, 8 rows, 4 button rows, 9 button cols, 6
// encoders, 1 OLED.
static constexpr DelugeDisplayDesc kDisplays[] = {{128, 48, 5}};
extern const DelugeBoard kDelugeBoard = {
    .name = "Synthstrom Deluge (144-pad, OLED)",
    .pad_grid_width = 16,
    .pad_grid_height = 8,
    .button_rows = 4,
    .button_cols = 9,
    .encoder_count = 6,
    .cv_channels = 2,
    .gate_channels = 4,
    .display_kind = DELUGE_DISPLAY_OLED,
    .display_width = 128,
    .display_height = 48,
    .display_count = 1,
    .displays = kDisplays,
    .audio_sample_rate_hz = 44100,
    .audio_in_channels = 2,
    .audio_out_channels = 2,
};

const BoardProfile& deluge_profile() {
	static const BoardProfile profile{
	    .button_by_matrix = kMatrix,
	    .led_by_button = kLedByButton,
	    .encoder_by_index = kEncoders,
	    .encoder_kind = kKinds,
	    .oled_role_by_index = kRoles,
	    .style_by_index = kStyles,
	    // No l10n bridge yet, stated rather than left to the default: the `l10n::String` table
	    // these resolve against is spec E's, and wiring the firmware's copy of it to this profile
	    // is spec F's. Until then every string id and error code shows as its own digits, which is
	    // what `BoardProfile::text_or_digits`/`error_text_or_digits` do with a null hook -- not a
	    // blank popup.
	    .text = nullptr,
	    .error_text = nullptr,
	    .button_cols = 9,
	    .button_rows = 4,
	    .sidebar_cols = 2,
	};
	return profile;
}

} // namespace deluge::ui::shell
