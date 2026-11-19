#include "shell/profile.h"
#include "shell/profiles/deluge.h"
#include "shell/profiles/test_board.h"
#include "support/mocks/fake_surfaces.h"
#include "ui/core/input.h"

#include "cppspec.hpp"

#include <string>

#include "support/spec_main.h"
using namespace deluge::ui::shell;
using namespace deluge::ui;

// clang-format off
describe shell_profiles("shell::board profiles", $ {
	it("maps every legacy KnownButton coordinate to the expected ButtonId", _ {
		const BoardProfile& profile = deluge_profile();
		// Table-driven: every button coordinate from the `*ButtonCoord` constants (`definitions_cxx.hpp`) and
		// `ZmodButtonX` (`button.h`).
		const struct {
			int x, y;
			ButtonId expected;
		} button_table[] = {
			// Encoder buttons: column 0
			{0, 0, ButtonId::YEnc},
			{0, 1, ButtonId::XEnc},
			{0, 2, ButtonId::ModEnc0},
			{0, 3, ButtonId::ModEnc1},
			// Mod buttons: columns 1-2
			{1, 0, ButtonId::Mod0}, {2, 0, ButtonId::Mod4},
			{1, 1, ButtonId::Mod1}, {2, 1, ButtonId::Mod5},
			{1, 2, ButtonId::Mod2}, {2, 2, ButtonId::Mod6},
			{1, 3, ButtonId::Mod3}, {2, 3, ButtonId::Mod7},
			// Menu buttons: column 3
			{3, 0, ButtonId::AffectEntire}, {3, 1, ButtonId::Session},
			{3, 2, ButtonId::Clip}, {3, 3, ButtonId::Keyboard},
			// None: column 4
			{4, 0, ButtonId::None}, {4, 2, ButtonId::None},
			// Tempo encoder and others: column 4
			{4, 1, ButtonId::TempoEnc}, {4, 3, ButtonId::SelectEnc},
			// Type buttons: column 5
			{5, 0, ButtonId::Synth}, {5, 1, ButtonId::Kit},
			{5, 2, ButtonId::Midi}, {5, 3, ButtonId::Cv},
			// File buttons: column 6
			{6, 0, ButtonId::Scale}, {6, 1, ButtonId::Load},
			{6, 2, ButtonId::CrossScreen}, {6, 3, ButtonId::Save},
			// View buttons: column 7
			{7, 0, ButtonId::Learn}, {7, 1, ButtonId::Back},
			{7, 2, ButtonId::SyncScaling}, {7, 3, ButtonId::TapTempo},
			// Transport: column 8
			{8, 0, ButtonId::Shift}, {8, 1, ButtonId::Triplets},
			{8, 2, ButtonId::Record}, {8, 3, ButtonId::Play},
		};
		for (const auto& row : button_table) {
			const size_t index = row.y * 9 + row.x;
			expect(profile.button_by_matrix[index]).to_equal(row.expected);
		}
	});
	it("LED indices follow x + y*9 and encoder buttons have none", _ {
		const BoardProfile& profile = deluge_profile();
		// Table-driven: every LED-bearing button from the `LED` enum (`indicator_leds.h`).
		const struct {
			ButtonId button;
			int8_t expected_led;
		} led_table[] = {
			{ButtonId::AffectEntire, 3},     // (3, 0) -> `LED::AFFECT_ENTIRE` (`indicator_leds.h`)
			{ButtonId::Session, 12},         // (3, 1) -> `LED::SESSION_VIEW` (`indicator_leds.h`)
			{ButtonId::Clip, 21},            // (3, 2) -> `LED::CLIP_VIEW` (`indicator_leds.h`)
			{ButtonId::Synth, 5},            // (5, 0) -> `LED::SYNTH` (`indicator_leds.h`)
			{ButtonId::Kit, 14},             // (5, 1) -> `LED::KIT` (`indicator_leds.h`)
			{ButtonId::Midi, 23},            // (5, 2) -> `LED::MIDI` (`indicator_leds.h`)
			{ButtonId::Cv, 32},              // (5, 3) -> `LED::CV` (`indicator_leds.h`)
			{ButtonId::Keyboard, 30},        // (3, 3) -> `LED::KEYBOARD` (`indicator_leds.h`)
			{ButtonId::Scale, 6},            // (6, 0) -> `LED::SCALE_MODE` (`indicator_leds.h`)
			{ButtonId::CrossScreen, 24},     // (6, 2) -> `LED::CROSS_SCREEN_EDIT` (`indicator_leds.h`)
			{ButtonId::Back, 16},            // (7, 1) -> `LED::BACK` (`indicator_leds.h`)
			{ButtonId::Load, 15},            // (6, 1) -> `LED::LOAD` (`indicator_leds.h`)
			{ButtonId::Save, 33},            // (6, 3) -> `LED::SAVE` (`indicator_leds.h`)
			{ButtonId::Learn, 7},            // (7, 0) -> `LED::LEARN` (`indicator_leds.h`)
			{ButtonId::TapTempo, 34},        // (7, 3) -> `LED::TAP_TEMPO` (`indicator_leds.h`)
			{ButtonId::SyncScaling, 25},     // (7, 2) -> `LED::SYNC_SCALING` (`indicator_leds.h`)
			{ButtonId::Triplets, 17},        // (8, 1) -> `LED::TRIPLETS` (`indicator_leds.h`)
			{ButtonId::Play, 35},            // (8, 3) -> `LED::PLAY` (`indicator_leds.h`)
			{ButtonId::Record, 26},          // (8, 2) -> `LED::RECORD` (`indicator_leds.h`)
			{ButtonId::Shift, 8},            // (8, 0) -> `LED::SHIFT` (`indicator_leds.h`)
			{ButtonId::Mod0, 1},             // (1, 0) -> `LED::MOD_0` (`indicator_leds.h`)
			{ButtonId::Mod1, 10},            // (1, 1) -> `LED::MOD_1` (`indicator_leds.h`)
			{ButtonId::Mod2, 19},            // (1, 2) -> `LED::MOD_2` (`indicator_leds.h`)
			{ButtonId::Mod3, 28},            // (1, 3) -> `LED::MOD_3` (`indicator_leds.h`)
			{ButtonId::Mod4, 2},             // (2, 0) -> `LED::MOD_4` (`indicator_leds.h`)
			{ButtonId::Mod5, 11},            // (2, 1) -> `LED::MOD_5` (`indicator_leds.h`)
			{ButtonId::Mod6, 20},            // (2, 2) -> `LED::MOD_6` (`indicator_leds.h`)
			{ButtonId::Mod7, 29},            // (2, 3) -> `LED::MOD_7` (`indicator_leds.h`)
		};
		for (const auto& row : led_table) {
			expect(profile.led_by_button[static_cast<size_t>(row.button)]).to_equal(row.expected_led);
		}
		// Encoder buttons have no LEDs.
		const ButtonId encoder_buttons[] = {ButtonId::SelectEnc, ButtonId::TempoEnc, ButtonId::XEnc, ButtonId::YEnc, ButtonId::ModEnc0, ButtonId::ModEnc1};
		for (const auto& btn : encoder_buttons) {
			expect(profile.led_by_button[static_cast<size_t>(btn)]).to_equal(int8_t(-1));
		}
	});
	it("encoder index order matches encoder_input.cpp", _ {
		const BoardProfile& profile = deluge_profile();
		// 0 = Vertical (scrollY)
		expect(profile.encoder_by_index[0]).to_equal(EncoderRole::Vertical);
		// 1 = Horizontal (scrollX)
		expect(profile.encoder_by_index[1]).to_equal(EncoderRole::Horizontal);
		// 2 = Tempo
		expect(profile.encoder_by_index[2]).to_equal(EncoderRole::Tempo);
		// 3 = Select
		expect(profile.encoder_by_index[3]).to_equal(EncoderRole::Select);
		// 4 = GoldUpper (mod1)
		expect(profile.encoder_by_index[4]).to_equal(EncoderRole::GoldUpper);
		// 5 = GoldLower (mod0)
		expect(profile.encoder_by_index[5]).to_equal(EncoderRole::GoldLower);
		// GoldUpper is Accelerating
		expect(profile.encoder_kind[static_cast<size_t>(EncoderRole::GoldUpper)]).to_equal(EncoderKind::Accelerating);
		// GoldLower is Accelerating
		expect(profile.encoder_kind[static_cast<size_t>(EncoderRole::GoldLower)]).to_equal(EncoderKind::Accelerating);
		// Select is Detented
		expect(profile.encoder_kind[static_cast<size_t>(EncoderRole::Select)]).to_equal(EncoderKind::Detented);
	});
	it("the test board has three OLEDs in the expected roles", _ {
		const BoardProfile& profile = test_board_profile();
		// Verify OLED roles
		expect(profile.oled_role_by_index.size()).to_equal(size_t(3));
		expect(profile.oled_role_by_index[0]).to_equal(SurfaceRole::Main);
		expect(profile.oled_role_by_index[1]).to_equal(SurfaceRole::Param0);
		expect(profile.oled_role_by_index[2]).to_equal(SurfaceRole::Aux);
	});
	it("Deluge profile has one OLED in Main role", _ {
		const BoardProfile& profile = deluge_profile();
		expect(profile.oled_role_by_index.size()).to_equal(size_t(1));
		expect(profile.oled_role_by_index[0]).to_equal(SurfaceRole::Main);
	});
	it("Deluge profile has no l10n bridge yet, and falls back to digits", _ {
		// The l10n table is not wired into this profile, so both hooks are null on the
		// shipping profile. Pinned so the fallback path stays the one the real board takes, and so
		// a future wiring is a deliberate edit to this expectation rather than a silent change of
		// what an error popup says.
		const BoardProfile& profile = deluge_profile();
		expect(profile.text).to_be_null();
		expect(profile.error_text).to_be_null();

		TextScratch scratch{};
		expect(std::string(profile.text_or_digits(42, scratch))).to_equal(std::string("42"));
		expect(std::string(profile.error_text_or_digits(-7, scratch))).to_equal(std::string("-7"));
	});
});
// clang-format on

DELUGE_SPEC(profiles_spec, shell_profiles)
