#include "deluge.h"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host/shell_host.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"

namespace deluge::gui::shell_host {
namespace {

/// The device's own actions, each the legacy call it names.
class HostDeviceActions final : public deluge::ui::scenes::bind::DeviceActions {
public:
	void save_settings() override {
		// `View::endMIDILearn` (`origin/main:src/deluge/gui/views/view.cpp`) writes the settings file alone.
		FlashStorage::writeSettings();
	}
	void delete_all_logs() override { actionLogger.deleteAllLogs(); }
	// The resets are `ResetSettings::acceptCurrentOption`'s arms (`origin/main:src/deluge/gui/context_menu/
	// reset_settings.cpp`). The ones whose settings main reads live re-apply them, so the open guest and menu see them.
	void reset_settings() override {
		FlashStorage::factoryReset();
		gui::ui::reapplyLiveSettingsAfterReset();
	}
	void reset_community_features() override {
		runtimeFeatureSettings.factoryReset();
		gui::ui::reapplyLiveSettingsAfterReset();
	}
	void reset_midi_follow() override { midiFollow.factoryReset(); }
	void reset_midi_devices() override { MIDIDeviceManager::factoryReset(); }
	void factory_reset() override {
		Deluge::factoryReset();
		gui::ui::reapplyLiveSettingsAfterReset();
	}
};

} // namespace

deluge::ui::scenes::bind::DeviceActions& device_actions() {
	static HostDeviceActions actions;
	return actions;
}

} // namespace deluge::gui::shell_host
