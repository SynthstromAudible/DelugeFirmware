#include "definitions_cxx.hpp"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h"
#include "gui/views/performance_view.h"

#include <version.h>

#include <cstdint>
#include <string_view>
#include <utility>

// The battery's voltage, which the main loop sets (src/deluge/deluge.cpp). Declared at global scope: a namespaced
// declaration would name a different, unlinked variable.
extern uint16_t batteryMV;

namespace deluge::gui::shell_host {
namespace {

using deluge::ui::scenes::bind::PerformanceEditing;

// The port converts these by value.
static_assert(std::to_underlying(PerformanceEditing::Disabled) == std::to_underlying(PerformanceEditingMode::DISABLED));
static_assert(std::to_underlying(PerformanceEditing::Value) == std::to_underlying(PerformanceEditingMode::VALUE));
static_assert(std::to_underlying(PerformanceEditing::Param) == std::to_underlying(PerformanceEditingMode::PARAM));

class HostUiFacts final : public deluge::ui::scenes::bind::UiFacts {
public:
	[[nodiscard]] bool auditioning() const override { return isUIModeActive(UI_MODE_AUDITIONING); }
	[[nodiscard]] bool popup_showing() const override {
		// `display->hasPopup()` for the guest: its popups and notifications are scenes on its stack.
		for (const deluge::ui::shell::Scene* scene : deluge::gui::ui::shellRuntime().shell().stack().scenes()) {
			if (scene->kind() == deluge::ui::shell::SceneKind::Popup
			    || scene->kind() == deluge::ui::shell::SceneKind::Notification) {
				return true;
			}
		}
		return false;
	}
	[[nodiscard]] PerformanceEditing performance_editing_mode() const override {
		// `EditingMode::readCurrentValue`: `defaultEditingMode` first, then `editingParam`.
		if (!performanceView.defaultEditingMode) {
			return PerformanceEditing::Disabled;
		}
		return performanceView.editingParam ? PerformanceEditing::Param : PerformanceEditing::Value;
	}
	[[nodiscard]] int32_t battery_millivolts() const override { return batteryMV; }
	[[nodiscard]] std::string_view firmware_version() const override { return kFirmwareVersionString; }
};

} // namespace

const deluge::ui::scenes::bind::UiFacts& ui_facts() {
	static const HostUiFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
