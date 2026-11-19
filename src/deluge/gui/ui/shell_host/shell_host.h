#pragma once

/// @file
/// @brief The app's implementations of the scenes' ports, one per file in this directory, and the one set of ports
///        built from them. Only these files name the legacy runtime objects the scenes read.

#include "scenes/bind/ports/ports.h"

namespace deluge::gui::shell_host {

/// @brief The host's AudioFacts.
[[nodiscard]] const scenes::bind::AudioFacts& audio_facts();
/// @brief The host's CardFacts.
[[nodiscard]] const scenes::bind::CardFacts& card_facts();
/// @brief The host's UiFacts.
[[nodiscard]] const scenes::bind::UiFacts& ui_facts();

/// @brief The host's SoundFacts.
[[nodiscard]] const scenes::bind::SoundFacts& sound_facts();

} // namespace deluge::gui::shell_host
