#pragma once

/// @file
/// @brief The app's ports, which a menu opened in the app binds its target nodes through, and the app's hooks a scene
/// calls. Declared here and defined by the app, as the scenes link the app: in gui/ui/shell_host/ (the ports by
/// ports.cpp, the waveforms by waveforms.cpp, the rest by menu_hooks.cpp).

#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/services/favourites.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::menu {

/// @brief The app's ports, which every scene's context takes; valid for the program's life.
[[nodiscard]] const deluge::ui::scenes::bind::Ports& host_ports();

/// @brief A flash, community-features or full reset ran: the app re-applies, on the guest and the open menu, the reset
///        settings main reads live.
void host_settings_reset();

/// @brief The file the sample browser last started to preview or load (`deluge::model::lastFilePathLoaded`),
///        which a sample browser opens beside when its holder names no file in SAMPLES/.
std::string host_last_sample_path();
/// @brief Stop the preview's sound and drop the sample file @p path from memory, unless the song uses it: the sample
///        browser's Shift+SAVE, before it offers to delete the file (`SampleBrowser::buttonAction`).
/// @return False when the song uses the file, which may then not be deleted.
bool host_release_sample_file(std::string_view path);
/// @brief The favourites bank the app's browsers share.
deluge::ui::scenes::services::Favourites& host_favourites();

} // namespace deluge::ui::scenes::menu
