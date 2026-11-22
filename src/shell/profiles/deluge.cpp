#include "deluge.h"
#include "libdeluge/board.h"
#include "shell/profile.h"
#include "ui/widgets/core/input.h"
#include "ui/widgets/core/style.h"

namespace deluge::ui::shell {

// Button matrix: 4 rows × 9 columns, row-major order (y * 9 + x).
// Coordinates from the `*ButtonCoord` constants (`definitions_cxx.hpp`) and `ZmodButtonX`/`ZmodButtonY` (`button.h`).
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

// LED indices by ButtonId: x + y*9 for buttons with LEDs (from `LED::AFFECT_ENTIRE` through `LED::SHIFT` in
// `indicator_leds.h`), -1 for encoder buttons. All values from legacy LED enum: `indicator_leds.h`.
static constexpr int8_t kLedByButton[] = {
    -1, // None
    3,  // AffectEntire (3, 0) -> 3; `LED::AFFECT_ENTIRE` (`indicator_leds.h`)
    12, // Session (3, 1) -> 12; `LED::SESSION_VIEW` (`indicator_leds.h`)
    21, // Clip (3, 2) -> 21; `LED::CLIP_VIEW` (`indicator_leds.h`)
    5,  // Synth (5, 0) -> 5; `LED::SYNTH` (`indicator_leds.h`)
    14, // Kit (5, 1) -> 14; `LED::KIT` (`indicator_leds.h`)
    23, // Midi (5, 2) -> 23; `LED::MIDI` (`indicator_leds.h`)
    32, // Cv (5, 3) -> 32; `LED::CV` (`indicator_leds.h`)
    30, // Keyboard (3, 3) -> 30; `LED::KEYBOARD` (`indicator_leds.h`)
    6,  // Scale (6, 0) -> 6; `LED::SCALE_MODE` (`indicator_leds.h`)
    24, // CrossScreen (6, 2) -> 24; `LED::CROSS_SCREEN_EDIT` (`indicator_leds.h`)
    16, // Back (7, 1) -> 16; `LED::BACK` (`indicator_leds.h`)
    15, // Load (6, 1) -> 15; `LED::LOAD` (`indicator_leds.h`)
    33, // Save (6, 3) -> 33; `LED::SAVE` (`indicator_leds.h`)
    7,  // Learn (7, 0) -> 7; `LED::LEARN` (`indicator_leds.h`)
    34, // TapTempo (7, 3) -> 34; `LED::TAP_TEMPO` (`indicator_leds.h`)
    25, // SyncScaling (7, 2) -> 25; `LED::SYNC_SCALING` (`indicator_leds.h`)
    17, // Triplets (8, 1) -> 17; `LED::TRIPLETS` (`indicator_leds.h`)
    35, // Play (8, 3) -> 35; `LED::PLAY` (`indicator_leds.h`)
    26, // Record (8, 2) -> 26; `LED::RECORD` (`indicator_leds.h`)
    8,  // Shift (8, 0) -> 8; `LED::SHIFT` (`indicator_leds.h`)
    1,  // Mod0 (1, 0) -> 1; `LED::MOD_0` (`indicator_leds.h`)
    10, // Mod1 (1, 1) -> 10; `LED::MOD_1` (`indicator_leds.h`)
    19, // Mod2 (1, 2) -> 19; `LED::MOD_2` (`indicator_leds.h`)
    28, // Mod3 (1, 3) -> 28; `LED::MOD_3` (`indicator_leds.h`)
    2,  // Mod4 (2, 0) -> 2; `LED::MOD_4` (`indicator_leds.h`)
    11, // Mod5 (2, 1) -> 11; `LED::MOD_5` (`indicator_leds.h`)
    20, // Mod6 (2, 2) -> 20; `LED::MOD_6` (`indicator_leds.h`)
    29, // Mod7 (2, 3) -> 29; `LED::MOD_7` (`indicator_leds.h`)
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

// Encoder order from encoder_input.cpp's interpretEncoders: 0=scrollY 1=scrollX 2=tempo 3=select 4=mod1(upper)
// 5=mod0(lower). 4=mod1=upper gold; 5=mod0=lower gold (encoders.h's mod0/mod1).
static constexpr widgets::core::EncoderRole kEncoders[] = {
    widgets::core::EncoderRole::Vertical,   // 0
    widgets::core::EncoderRole::Horizontal, // 1
    widgets::core::EncoderRole::Tempo,      // 2
    widgets::core::EncoderRole::Select,     // 3
    widgets::core::EncoderRole::GoldUpper,  // 4
    widgets::core::EncoderRole::GoldLower,  // 5
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
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == static_cast<size_t>(widgets::core::EncoderRole::Count));

// OLED roles: Deluge has one main OLED.
static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main};
static_assert(sizeof(kRoles) / sizeof(kRoles[0]) == 1);

// Display styles: Deluge has one 128×48 OLED.
static constexpr const widgets::core::Style* kStyles[] = {&widgets::core::kDelugeStyle48};
static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == 1);

// Export table spans for use by test_board and other profiles.
const std::span<const ButtonId> kDelugeButtonMatrix = kMatrix;
const std::span<const int8_t> kDelugeLedByButton = kLedByButton;
const std::span<const widgets::core::EncoderRole> kDelugeEncoders = kEncoders;
const std::span<const EncoderKind> kDelugeEncoderKinds = kKinds;
const std::span<const SurfaceRole> kDelugeOledRoles = kRoles;
const std::span<const widgets::core::Style* const> kDelugeOledStyles = kStyles;

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
	    // No l10n bridge, stated rather than left to the default: the profile is not wired to the
	    // firmware's `l10n::String` table, so every string id and error code shows as its own
	    // digits, which is what `BoardProfile::text_or_digits`/`error_text_or_digits` do with a
	    // null hook -- not a blank popup.
	    .text = nullptr,
	    .error_text = nullptr,
	    .button_cols = 9,
	    .button_rows = 4,
	    .sidebar_cols = 2,
	};
	return profile;
}

} // namespace deluge::ui::shell
