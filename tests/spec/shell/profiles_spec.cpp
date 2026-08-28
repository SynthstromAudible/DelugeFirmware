#include "shell/profile.h"
#include "shell/profiles/deluge.h"
#include "shell/profiles/test_board.h"
#include "support/fake_surfaces.h"
#include "ui/core/input.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using namespace deluge::ui;

// clang-format off
describe shell_profiles("shell::board profiles", $ {
	it("maps every legacy KnownButton coordinate to the expected ButtonId", _ {
		const BoardProfile& profile = deluge_profile();
		// Sample a few known mappings from definitions_cxx.hpp:59-84
		// YEnc at (0, 0)
		expect(profile.button_by_matrix[0 * 9 + 0]).to_equal(ButtonId::YEnc);
		// XEnc at (0, 1)
		expect(profile.button_by_matrix[1 * 9 + 0]).to_equal(ButtonId::XEnc);
		// Shift at (8, 0)
		expect(profile.button_by_matrix[0 * 9 + 8]).to_equal(ButtonId::Shift);
		// Play at (8, 3)
		expect(profile.button_by_matrix[3 * 9 + 8]).to_equal(ButtonId::Play);
		// AffectEntire at (3, 0)
		expect(profile.button_by_matrix[0 * 9 + 3]).to_equal(ButtonId::AffectEntire);
		// Mod2 at (1, 2)
		expect(profile.button_by_matrix[2 * 9 + 1]).to_equal(ButtonId::Mod2);
		// Mod7 at (2, 3)
		expect(profile.button_by_matrix[3 * 9 + 2]).to_equal(ButtonId::Mod7);
	});
	it("LED indices follow x + y*9 and encoder buttons have none", _ {
		const BoardProfile& profile = deluge_profile();
		// Play at (8, 3) -> LED index 8 + 3*9 = 35
		expect(profile.led_by_button[static_cast<size_t>(ButtonId::Play)]).to_equal(int8_t(35));
		// Record at (8, 2) -> LED index 8 + 2*9 = 26
		expect(profile.led_by_button[static_cast<size_t>(ButtonId::Record)]).to_equal(int8_t(26));
		// XEnc has no LED
		expect(profile.led_by_button[static_cast<size_t>(ButtonId::XEnc)]).to_equal(int8_t(-1));
		// YEnc has no LED
		expect(profile.led_by_button[static_cast<size_t>(ButtonId::YEnc)]).to_equal(int8_t(-1));
		// ModEnc0 has no LED
		expect(profile.led_by_button[static_cast<size_t>(ButtonId::ModEnc0)]).to_equal(int8_t(-1));
		// ModEnc1 has no LED
		expect(profile.led_by_button[static_cast<size_t>(ButtonId::ModEnc1)]).to_equal(int8_t(-1));
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
});
// clang-format on

DELUGE_SPEC(shell_profiles_spec, shell_profiles)
