#pragma once

/// @file
/// @brief The device's shortcut layout as a spec reads and sets it with no menu open: through a context over the
///        host's ports, as the menu itself reads it.

#include "deluge-project/device_settings.h"
#include "scenes/bind/ports/device_settings.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"
#include "scenes/menu/shortcuts.h"

#include <utility>

namespace deluge::scenes::spec {

/// @brief A context that binds only the host's ports: what the menu reads the device's own settings through.
inline menu::MenuContext host_ports_context() {
	menu::MenuContext ctx{};
	ctx.ports = menu::host_ports();
	return ctx;
}

/// @brief The shortcut layout the device's `pads/shortcutsVersion` row holds now.
inline project::ShortcutsVersion shortcut_layout() {
	return menu::shortcuts_version(host_ports_context());
}

/// @brief Writes @p version to the device's `pads/shortcutsVersion` row, as the settings item does.
inline void set_shortcut_layout(project::ShortcutsVersion version) {
	if (const bind::Ports& ports = menu::host_ports(); ports.device != nullptr) {
		(void)ports.device->write(project::device_path::kShortcutsVersion, std::to_underlying(version));
	}
}

/// @brief The shortcut layout set for a scope and put back after.
struct ShortcutLayout {
	project::ShortcutsVersion saved = shortcut_layout();
	explicit ShortcutLayout(project::ShortcutsVersion version) { set_shortcut_layout(version); }
	~ShortcutLayout() { set_shortcut_layout(saved); }
	ShortcutLayout(const ShortcutLayout&) = delete;
	ShortcutLayout& operator=(const ShortcutLayout&) = delete;
};

} // namespace deluge::scenes::spec
