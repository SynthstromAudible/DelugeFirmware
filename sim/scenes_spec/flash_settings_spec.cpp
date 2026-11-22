/// @file
/// @brief Settings that live in `FlashStorage` and what reads them: flash byte 60's shortcut layout, the pad colours
///        and the default-range draws.

#include "gui/colour/colour.h"
#include "gui/colour/pad_colour.h"
#include "libdeluge/flash.h"
#include "storage/flash_storage.h"
#include "support/host_flash.h"
#include "support/printers/rgb_printer.h"
#include "ui/scenes/menu/shortcuts.h"
#include "util/functions.h"
#include "util/synth_noise.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "support/shortcut_layout.h"
#include "support/spec_main.h"

namespace menu = deluge::ui::scenes::menu;
namespace spec = deluge::ui::scenes::spec;

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

/// @brief Save the settings, put @p value in flash byte @p at, and read the settings back, as a boot reads a sector
///        another firmware wrote.
void read_settings_with_byte(uint32_t at, uint8_t value) {
	FlashStorage::writeSettings();
	uint8_t sector[256];
	deluge_flash_read(0, sector, sizeof(sector));
	sector[at] = value;
	deluge_flash_erase(0);
	deluge_flash_program(0, sector, sizeof(sector));
	FlashStorage::readSettings();
}
} // namespace

// clang-format off
describe flash_shortcuts_byte("the shortcut layout is flash byte 60", $ {
	it("selecting a layout writes the byte the settings write stores", _ {
		ShortcutsByteGuard guard;
		spec::set_shortcut_layout(deluge::project::ShortcutsVersion::V1);
		expect(static_cast<int32_t>(FlashStorage::shortcutsVersion)).to_equal(0);
		spec::set_shortcut_layout(deluge::project::ShortcutsVersion::V3);
		expect(static_cast<int32_t>(FlashStorage::shortcutsVersion)).to_equal(1);
	});

	it("reads the byte for the layout, any value but 0 as the current one, as main's switch does", _ {
		ShortcutsByteGuard guard;
		FlashStorage::shortcutsVersion = 0;
		expect(spec::shortcut_layout()).to_equal(deluge::project::ShortcutsVersion::V1);
		FlashStorage::shortcutsVersion = 1;
		expect(spec::shortcut_layout()).to_equal(deluge::project::ShortcutsVersion::V3);
		FlashStorage::shortcutsVersion = 7;
		expect(spec::shortcut_layout()).to_equal(deluge::project::ShortcutsVersion::V3);
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
			expect(deluge::gui::padColourRGB(FlashStorage::PAD_COLOUR_ACTIVE)).to_equal(want[v]);
		}
	});

	it("reads the slot it is asked for", _ {
		SettingsGuard guard;
		FlashStorage::padColours[FlashStorage::PAD_COLOUR_ACTIVE] = deluge::gui::RED;
		FlashStorage::padColours[FlashStorage::PAD_COLOUR_MUTED] = deluge::gui::BLUE;
		expect(deluge::gui::padColourRGB(FlashStorage::PAD_COLOUR_MUTED)).to_equal(deluge::gui::colours::blue);
	});

	it("reads a stored colour past the nine as the slot's default, which has a colour to paint", _ {
		SettingsGuard guard;
		deluge::ui::scenes::spec::ErasedFlash flash;
		FlashStorage::padColours[FlashStorage::PAD_COLOUR_ACTIVE] = deluge::gui::PINK;
		read_settings_with_byte(74, deluge::gui::kNumPadColours); // byte 74 is the active colour
		expect(static_cast<int32_t>(FlashStorage::padColours[FlashStorage::PAD_COLOUR_ACTIVE]))
		    .to_equal(static_cast<int32_t>(deluge::gui::GREEN));
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
			FlashStorage::defaultRanges[slot] = {40, 42};
			for (uint32_t seed = 1; seed <= 16; seed++) {
				deluge_seed_noise(seed);
				const int32_t want = 40 + random(2);
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

	it("reads a key bound past the octave as stored and draws it unreduced, as main's readSettings keeps it", _ {
		SettingsGuard guard;
		deluge::ui::scenes::spec::ErasedFlash flash;
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {120, 120}; // byte 53 nonzero: the ranges read
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {0, 200};
		read_settings_with_byte(57, 200); // byte 57 is the key's lower bound
		expect(static_cast<int32_t>(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].lower)).to_equal(200);
		expect(FlashStorage::drawDefaultRange(FlashStorage::DEFAULT_RANGE_KEY)).to_equal(200);
	});
});
// clang-format on

DELUGE_SPEC(flash_settings_spec, flash_shortcuts_byte, flash_pad_colours, flash_default_draws)
