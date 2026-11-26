#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host/shell_host.h"
#include "ui/scenes/menu/bind_host.h"

namespace deluge::ui::scenes::menu {

void host_settings_reset() {
	gui::ui::reapplyLiveSettingsAfterReset();
}

} // namespace deluge::ui::scenes::menu
