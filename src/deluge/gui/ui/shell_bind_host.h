#pragma once

/// @file
/// @brief The app's side of the scenes' binding (gui/ui/shell_bind_host.cpp): the sink that applies a menu's writes as
/// Edits and the runtime facts a menu reads, reached by the scenes through scenes/menu/bind_host.h. Host builds only.

#include <cstdint>

namespace deluge::gui {

/// @brief How many menu writes the app's sink has refused since start-up: a request whose slot does not resolve, a
/// sound with no slot, or a value its Edit refused. Each is also logged.
[[nodiscard]] uint32_t shellEditRefusals();

} // namespace deluge::gui
