/// @file
/// @brief Settings that live in `FlashStorage` and what reads them: flash byte 60's shortcut layout, the pad colours
///        and the default-range draws.

#include "gui/colour/colour.h"
#include "gui/colour/pad_colour.h"
#include "scenes/menu/shortcuts.h"
#include "storage/flash_storage.h"
#include "util/functions.h"
#include "util/synth_noise.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "spec/support/spec_main.h"

namespace menu = deluge::scenes::menu;

namespace {
/// @brief Flash byte 60, put back however the example ends.
struct ShortcutsByteGuard {
	uint8_t saved = FlashStorage::shortcutsVersion;
	ShortcutsByteGuard() = default;
	~ShortcutsByteGuard() { FlashStorage::shortcutsVersion = saved; }
	ShortcutsByteGuard(const ShortcutsByteGuard&) = delete;
	ShortcutsByteGuard& operator=(const ShortcutsByteGuard&) = delete;
};

/// @brief The six pad colours and the three default ranges, put back however the example ends.
struct SettingsGuard {
	uint8_t colours[FlashStorage::NUM_PAD_COLOUR_SLOTS];
	FlashStorage::DefaultRange ranges[FlashStorage::NUM_DEFAULT_RANGE_SLOTS];
	SettingsGuard() {
		std::copy(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours), std::begin(colours));
		std::copy(std::begin(FlashStorage::defaultRanges), std::end(FlashStorage::defaultRanges), std::begin(ranges));
	}
	~SettingsGuard() {
		std::copy(std::begin(colours), std::end(colours), std::begin(FlashStorage::padColours));
		std::copy(std::begin(ranges), std::end(ranges), std::begin(FlashStorage::defaultRanges));
	}
	SettingsGuard(const SettingsGuard&) = delete;
	SettingsGuard& operator=(const SettingsGuard&) = delete;
};
} // namespace

// clang-format off
describe flash_shortcuts_byte("the shortcut layout is flash byte 60", $ {
	it("selecting a layout writes the byte the settings write stores", _ {
		ShortcutsByteGuard guard;
		menu::set_shortcuts_version(menu::ShortcutsVersion::V1);
		expect(static_cast<int32_t>(FlashStorage::shortcutsVersion)).to_equal(0);
		menu::set_shortcuts_version(menu::ShortcutsVersion::V3);
		expect(static_cast<int32_t>(FlashStorage::shortcutsVersion)).to_equal(1);
	});

	it("reads the byte for the layout, any value but 0 as the current one, as main's switch does", _ {
		ShortcutsByteGuard guard;
		FlashStorage::shortcutsVersion = 0;
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
		FlashStorage::shortcutsVersion = 1;
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();
		FlashStorage::shortcutsVersion = 7;
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();
	});
});

describe flash_pad_colours("a pad-colour setting paints main's colour for its value", $ {
	it("paints each of the nine values as main's Colour::getRGB does", _ {
		SettingsGuard guard;
		namespace c = deluge::gui::colours;
		const RGB want[deluge::gui::kNumPadColours] = {c::red,  c::enabled, c::blue,  c::yellow_orange, c::cyan,
		                                               c::magenta, c::amber, c::white, c::pink};
		for (uint8_t v = 0; v < deluge::gui::kNumPadColours; v++) {
			FlashStorage::padColours[FlashStorage::PAD_COLOUR_ACTIVE] = v;
			expect(deluge::gui::padColourRGB(FlashStorage::PAD_COLOUR_ACTIVE) == want[v]).to_be_true();
		}
	});

	it("reads the slot it is asked for", _ {
		SettingsGuard guard;
		FlashStorage::padColours[FlashStorage::PAD_COLOUR_ACTIVE] = deluge::gui::RED;
		FlashStorage::padColours[FlashStorage::PAD_COLOUR_MUTED] = deluge::gui::BLUE;
		expect(deluge::gui::padColourRGB(FlashStorage::PAD_COLOUR_MUTED) == deluge::gui::colours::blue).to_be_true();
	});
});

describe flash_default_draws("a new song's tempo, swing and key are drawn from the default ranges as main draws them", $ {
	it("draws the lower bound when the bounds are equal, and takes no random number", _ {
		SettingsGuard guard;
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {133, 133};
		deluge_seed_noise(1234);
		const int32_t next = random(1000);
		deluge_seed_noise(1234);
		expect(FlashStorage::drawDefaultRange(FlashStorage::DEFAULT_RANGE_TEMPO)).to_equal(133);
		expect(random(1000)).to_equal(next);
	});

	it("draws lower + random(upper - lower) for tempo and swing", _ {
		SettingsGuard guard;
		for (const FlashStorage::DefaultRangeSlot slot :
		     {FlashStorage::DEFAULT_RANGE_TEMPO, FlashStorage::DEFAULT_RANGE_SWING_AMOUNT}) {
			FlashStorage::defaultRanges[slot] = {40, 80};
			for (uint32_t seed = 1; seed <= 256; seed++) {
				deluge_seed_noise(seed);
				const int32_t want = 40 + random(40);
				deluge_seed_noise(seed);
				expect(FlashStorage::drawDefaultRange(slot)).to_equal(want);
			}
		}
	});

	it("draws the key across the octave's wrap and leaves the result unreduced, as main does", _ {
		SettingsGuard guard;
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {10, 2};
		for (uint32_t seed = 1; seed <= 16; seed++) {
			deluge_seed_noise(seed);
			const int32_t want = 10 + random(4);
			deluge_seed_noise(seed);
			expect(FlashStorage::drawDefaultRange(FlashStorage::DEFAULT_RANGE_KEY)).to_equal(want);
		}
	});
});
// clang-format on

DELUGE_SPEC(flash_settings_spec, flash_shortcuts_byte, flash_pad_colours, flash_default_draws)
