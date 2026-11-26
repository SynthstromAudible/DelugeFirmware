#pragma once

/// @file
/// @brief The app's ports, which a menu opened in the app binds its target nodes through, and the app's hooks a scene
/// calls. Declared here and defined by the app, as the scenes link the app: in gui/ui/shell_host/ (the ports by
/// ports.cpp, the hook by menu_hooks.cpp).

#include "ui/scenes/bind/ports/ports.h"

namespace deluge::ui::scenes::menu {

/// @brief The app's ports, which every scene's context takes; valid for the program's life.
[[nodiscard]] const deluge::ui::scenes::bind::Ports& host_ports();

/// @brief A flash, community-features or full reset ran: the app re-applies, on the guest and the open menu, the reset
///        settings main reads live.
void host_settings_reset();

} // namespace deluge::ui::scenes::menu
