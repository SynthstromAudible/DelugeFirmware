/// @file
/// @brief Settings that live in `FlashStorage` and what reads them: flash byte 60's shortcut layout, the pad colours
///        and the default-range draws.

#include "scenes/menu/shortcuts.h"
#include "storage/flash_storage.h"

#include "cppspec.hpp"

#include <cstdint>

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
// clang-format on

DELUGE_SPEC(flash_settings_spec, flash_shortcuts_byte)
