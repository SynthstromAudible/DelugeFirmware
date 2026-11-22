/// @file
/// @brief A card that still carries the removed menu-mode setting: the key is an unknown setting, written back
///        as it was read, and the menu the clip view's SELECT opens is the menu.

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/storage_manager.h"
#include "support/song_fixture.h"
#include "support/ui_mode_guard.h"

#include "support/spec_main.h"

#include <string>
#include <string_view>

namespace {

/// @brief The runtime settings as an example found them, put back however it ends (the unknown list included).
struct SettingsGuard {
	RuntimeFeatureSettings saved = runtimeFeatureSettings;
	~SettingsGuard() { runtimeFeatureSettings = saved; }
	SettingsGuard() = default;
	SettingsGuard(const SettingsGuard&) = delete;
	SettingsGuard& operator=(const SettingsGuard&) = delete;
};

/// @brief The root UI as an example found it, put back.
struct RootUIGuard {
	RootUI* saved = getRootUI();
	RootUIGuard() = default;
	~RootUIGuard() {
		if (saved != nullptr) {
			setRootUILowLevel(saved);
		}
	}
	RootUIGuard(const RootUIGuard&) = delete;
	RootUIGuard& operator=(const RootUIGuard&) = delete;
};

void parse(std::string_view body) {
	std::string xml = std::string{StorageManager::kXmlDocumentHead} + "<runtimeFeatureSettings>\n" + std::string{body}
	                  + "</runtimeFeatureSettings>\n";
	RuntimeFeatureSettings::parseSettingsBytes(&runtimeFeatureSettings, xml.data(), static_cast<uint32_t>(xml.size()));
}

/// @brief What the settings file would be written as now.
std::string written() {
	XMLSerializer writer;
	writer.setMemoryBased();
	runtimeFeatureSettings.serialiseSettings(writer);
	return std::string{writer.getBufferPtr(), static_cast<size_t>(writer.bytesWritten())};
}

constexpr std::string_view kStaleLine = R"(<setting name="legacyMenu" value="1")";

} // namespace

// clang-format off
describe stale_menu_setting("a card that still says legacyMenu=1", $ {
	it("writes no legacyMenu of its own: a fresh init knows no such setting", _ {
		SettingsGuard guard;
		runtimeFeatureSettings.init();
		expect(written()).not_().to_contain("legacyMenu");
	});

	it("writes the stored entry back once, after every setting it knows, which keep their values", _ {
		SettingsGuard guard;
		runtimeFeatureSettings.init();
		const std::string before = written();
		parse(R"(<setting name="legacyMenu" value="1" />)" "\n");
		const std::string after = written();
		const size_t at = after.find(kStaleLine);
		expect(after).to_contain(kStaleLine);
		expect(after.substr(at + 1)).not_().to_contain(kStaleLine);
		// Everything before the unknown entry is what a fresh init writes, up to its closing tag.
		const size_t close = before.rfind("</runtimeFeatureSettings>");
		expect(after.substr(0, close)).to_equal(before.substr(0, close));
	});

	it("opens the menu from the clip view's SELECT all the same", _ {
		SettingsGuard guard;
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		parse(R"(<setting name="legacyMenu" value="1" />)" "\n");
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		(void)instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC, false);
		expect(getCurrentUI()).to_equal(&shellHostUI);
		nullifyUIs();
	});
});
// clang-format on

DELUGE_SPEC(stale_menu_setting_spec, stale_menu_setting)
