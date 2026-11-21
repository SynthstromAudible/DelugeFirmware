#pragma once

/// @file
/// @brief The sample browser's scene kind, for scenes that name it without opening it.

#include "shell/scene.h"

#include <cstdint>

namespace deluge::scenes::browser {

/// @brief The sample browser's scene kind: `FirstApp + 3` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kBrowserSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 3);

} // namespace deluge::scenes::browser
