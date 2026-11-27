#pragma once

/// @file
/// @brief The clean-roundtrip check for one song: its model survives the clean format, but for the fields the clean
/// load clamped, and so does every output of it as a preset.

#include "project/document.h"
#include "serialization/value.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::project {

/// @brief Faults a spec injects into the check, to prove it catches them.
struct CleanFaults {
	std::function<void(serialization::Value&)> on_encoded; ///< applied to the clean document before it is written
	std::function<void(SongDocument&)> on_decoded;         ///< applied to the model read back
};

/// @brief What a passing song showed.
struct CleanSummary {
	std::vector<std::string> clamped; ///< the paths the clean load clamped
	std::size_t presets{0};           ///< the outputs checked as presets
};

/// @brief Check one song.
/// @param text    The song document.
/// @param is_json True when @p text is JSON, false when it is XML.
/// @param faults  Faults to inject; none by default.
/// @return What it showed, or what failed.
[[nodiscard]] std::expected<CleanSummary, std::string> check_clean(std::string_view text, bool is_json,
                                                                   const CleanFaults& faults = {});

} // namespace deluge::project
