#pragma once

/// @file
/// @brief `currentUIMode` cleared for a spec example and put back however the example ends.
///
/// Legacy's UI mode is a process-wide word that the ported menu's close now ends (`ShellHostUI::closeGuest`), so an
/// example that asserts on it, or that opens the menu over a mode, holds this guard to start from `UI_MODE_NONE` and
/// leave the next example the value it found.

#include "gui/ui/ui.h" // currentUIMode, UI_MODE_NONE

#include <cstdint>

namespace deluge::scenes::spec {

/// @brief `currentUIMode` set to `UI_MODE_NONE` for the guard's lifetime, then restored.
struct UiModeGuard {
	uint32_t saved = currentUIMode;
	UiModeGuard() { currentUIMode = UI_MODE_NONE; }
	~UiModeGuard() { currentUIMode = saved; }
	UiModeGuard(const UiModeGuard&) = delete;
	UiModeGuard& operator=(const UiModeGuard&) = delete;
};

} // namespace deluge::scenes::spec
