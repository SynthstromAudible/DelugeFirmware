#pragma once

/// @file
/// @brief The app's ports, which a menu opened in the app binds its target nodes through. Declared here and defined by
/// the app, in gui/ui/shell_host/ports.cpp, as the scenes link the app.

#include "ui/scenes/bind/ports/ports.h"

namespace deluge::ui::scenes::menu {

/// @brief The app's ports, which every scene's context takes; valid for the program's life.
[[nodiscard]] const deluge::ui::scenes::bind::Ports& host_ports();

} // namespace deluge::ui::scenes::menu
