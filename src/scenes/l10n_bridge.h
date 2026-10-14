#pragma once
#include <cstdint>
#include <string_view>

namespace deluge::scenes {

/// @brief The scenes layer's l10n bridge: resolve a string id to display text.
///
/// `l10n::getView(static_cast<l10n::String>(string_id))`. Matches `ui::shell::TextLookup`, so this
/// is what `deluge_profile()` (`profile.h`) fills the shell's `BoardProfile::text` hook with.
/// @param string_id The `l10n::String` id, as stored on disk or baked into a menu node.
/// @return The resolved text.
std::string_view text_lookup(uint16_t string_id);

/// @brief The scenes layer's error-code bridge: resolve an `Error` value to display text.
///
/// A transcription of the `getErrorMessage(Error)` switch (`src/deluge/hid/display/display.cpp:7`)
/// -- the scenes layer may not include `hid/`, so this is a second copy of that mapping, not a
/// call to it. Matches `ui::shell::ErrorLookup`, so this is what `deluge_profile()` fills the
/// shell's `BoardProfile::error_text` hook with.
/// @param error_code The `Error` value, as an `int16_t` (`ui::shell::ErrorLookup`'s signature).
/// @return The resolved text.
std::string_view error_lookup(int16_t error_code);

} // namespace deluge::scenes
