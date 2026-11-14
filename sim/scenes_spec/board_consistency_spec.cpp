// clang-format off
#include "board_controls.hpp"
#include "definitions_cxx.hpp"
#include "scenes/profile.h"
#include "shell/profile.h"
#include <cppspec.hpp>

#include <algorithm>

using deluge::ui::shell::ButtonId;
using deluge::ui::EncoderRole;

describe board_consistency("the Deluge's controls board and the shell's profile", ${
	const deluge::ui::shell::BoardProfile& profile = deluge::scenes::deluge_profile();

	it("has as many section buttons as the profile has Mod buttons, and as many as the model's modes", _{
		const auto mods = static_cast<int32_t>(std::count_if(profile.button_by_matrix.begin(), profile.button_by_matrix.end(), [](ButtonId button) {
			const auto id = static_cast<uint8_t>(button);
			return id >= static_cast<uint8_t>(ButtonId::Mod0) && id <= static_cast<uint8_t>(ButtonId::Mod7);
		}));
		expect(kDelugeControls.sections).to_equal(mods);
		expect(static_cast<int32_t>(kDelugeControls.sections)).to_equal(kNumModButtons);
	});
	it("has as many slots as the model's gold knobs, and the profile's gold encoders are the first two", _{
		expect(static_cast<int32_t>(kDelugeControls.slots)).to_equal(kNumPhysicalModKnobs);
		expect(profile.encoder_by_index[4]).to_equal(EncoderRole::GoldUpper);
		expect(profile.encoder_by_index[5]).to_equal(EncoderRole::GoldLower);
	});
	it("takes the pad grid and indicator LEDs from the board's dimensions", _{
		expect(static_cast<int32_t>(kDelugeControls.pad_columns)).to_equal(kDisplayWidth);
		expect(static_cast<int32_t>(kDelugeControls.pad_rows)).to_equal(kDisplayHeight);
		expect(static_cast<int32_t>(kDelugeControls.sidebar_columns)).to_equal(kSideBarWidth);
		expect(kDelugeControls.sidebar_columns).to_equal(profile.sidebar_cols);
		expect(static_cast<size_t>(kDelugeControls.indicator_leds)).to_equal(kNumGoldKnobIndicatorLEDs);
	});
});
// clang-format on

CPPSPEC_SPEC(board_consistency)
