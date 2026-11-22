// clang-format off
#include "board_controls.hpp"
#include "definitions_cxx.hpp"
#include "host/chains/pages.h"
#include "scenes/profile.h"
#include "ui/shell/profile.h"
#include <cppspec.hpp>

#include <algorithm>
#include <span>

using deluge::ui::shell::ButtonId;
using deluge::ui::widgets::core::EncoderRole;

describe board_consistency("the Deluge's controls board and the shell's profile", ${
	const deluge::ui::shell::BoardProfile& profile = deluge::scenes::deluge_profile();

	// The profile the spec reads. The neuter test swaps its gold roles in a copy.
	const auto role_index = [](std::span<const EncoderRole> roles, EncoderRole role) {
		for (size_t i = 0; i < roles.size(); i++) {
			if (roles[i] == role) {
				return static_cast<int32_t>(i);
			}
		}
		return int32_t{-1};
	};

	it("has as many section buttons as the profile has Mod buttons", _{
		const auto mods = static_cast<int32_t>(std::count_if(profile.button_by_matrix.begin(), profile.button_by_matrix.end(), [](ButtonId button) {
			const auto id = static_cast<uint8_t>(button);
			return id >= static_cast<uint8_t>(ButtonId::Mod0) && id <= static_cast<uint8_t>(ButtonId::Mod7);
		}));
		expect(static_cast<int32_t>(kDelugeControls.sections)).to_equal(mods);
	});
	it("has as many section buttons as the chain publisher has sections", _{
		expect(static_cast<uint32_t>(kDelugeControls.sections)).to_equal(deluge::host::chains::kSections);
	});
	it("names the Mod buttons contiguously, as ModKnobs::handle's range test assumes", _{
		expect(static_cast<int32_t>(ButtonId::Mod7) - static_cast<int32_t>(ButtonId::Mod0) + 1).to_equal(static_cast<int32_t>(kDelugeControls.sections));
		expect(static_cast<int32_t>(ButtonId::Mod1)).to_equal(static_cast<int32_t>(ButtonId::Mod0) + 1);
		expect(static_cast<int32_t>(ButtonId::Mod2)).to_equal(static_cast<int32_t>(ButtonId::Mod0) + 2);
		expect(static_cast<int32_t>(ButtonId::Mod3)).to_equal(static_cast<int32_t>(ButtonId::Mod0) + 3);
		expect(static_cast<int32_t>(ButtonId::Mod4)).to_equal(static_cast<int32_t>(ButtonId::Mod0) + 4);
		expect(static_cast<int32_t>(ButtonId::Mod5)).to_equal(static_cast<int32_t>(ButtonId::Mod0) + 5);
		expect(static_cast<int32_t>(ButtonId::Mod6)).to_equal(static_cast<int32_t>(ButtonId::Mod0) + 6);
	});
	it("has as many slots as the profile has gold encoders", _{
		const auto golds = static_cast<int32_t>(std::count_if(profile.encoder_by_index.begin(), profile.encoder_by_index.end(), [](EncoderRole role) {
			return role == EncoderRole::GoldLower || role == EncoderRole::GoldUpper;
		}));
		expect(static_cast<int32_t>(kDelugeControls.slots)).to_equal(golds);
	});
	it("turns the lower gold knob as slot 0 and the upper as slot 1, which ModKnobs::handle assumes", _{
		expect(profile.encoder_by_index[4]).to_equal(EncoderRole::GoldUpper);
		expect(profile.encoder_by_index[5]).to_equal(EncoderRole::GoldLower);
		expect(role_index(profile.encoder_by_index, EncoderRole::GoldLower)).to_equal(5);
		expect(role_index(profile.encoder_by_index, EncoderRole::GoldUpper)).to_equal(4);
	});
	it("has a press for each slot exactly when the profile has the ModEnc buttons", _{
		const auto has = [&profile](ButtonId id) {
			return std::count(profile.button_by_matrix.begin(), profile.button_by_matrix.end(), id) == 1;
		};
		const int32_t presses = (has(ButtonId::ModEnc0) ? 1 : 0) + (has(ButtonId::ModEnc1) ? 1 : 0);
		expect(kDelugeControls.slot_press).to_be_true();
		expect(presses).to_equal(static_cast<int32_t>(kDelugeControls.slots));
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
