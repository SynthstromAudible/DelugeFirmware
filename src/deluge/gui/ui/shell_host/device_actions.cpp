#include "gui/ui/shell_host/shell_host.h"
#include "model/action/action_logger.h"
#include "storage/flash_storage.h"

namespace deluge::gui::shell_host {
namespace {

/// The device's own actions, each the legacy call it names.
class HostDeviceActions final : public scenes::bind::DeviceActions {
public:
	void save_settings() override {
		// `View::endMIDILearn` (`origin/main:src/deluge/gui/views/view.cpp`) writes the settings file alone.
		FlashStorage::writeSettings();
	}
	void delete_all_logs() override { actionLogger.deleteAllLogs(); }
};

} // namespace

scenes::bind::DeviceActions& device_actions() {
	static HostDeviceActions actions;
	return actions;
}

} // namespace deluge::gui::shell_host
