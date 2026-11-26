#pragma once

/// @file
/// @brief The app's ports: every port's host implementation, gathered for the scenes' contexts and services.

#include "ui/scenes/bind/ports/ports.h"

namespace deluge::gui::shell_host {

/// @brief The app's ports, which every scene's context takes; valid for the program's life.
[[nodiscard]] const deluge::ui::scenes::bind::Ports& host_ports();

} // namespace deluge::gui::shell_host
