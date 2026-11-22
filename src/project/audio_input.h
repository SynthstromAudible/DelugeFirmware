#pragma once

/// @file
/// @brief An audio track's input, in the words its file uses.
///
/// The record stores each as its index here, which is the engine's enum value: `AudioOutputMode` for the mode and
/// `AudioInputChannel` (from NONE) for the channel.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::project {

/// An audio track's mode, as `aoModeToString` writes it.
inline constexpr std::string_view kAudioModeWords[] = {"Player", "Sampler", "Looper/FX"};

/// An audio track's input channel, as `inputChannelToString` writes it; the index is the channel's enum value.
inline constexpr std::string_view kInputChannelWords[] = {"none", "left", "right",  "stereo",       "balanced",
                                                          "usb",  "mix",  "output", "specificTrack"};

/// The input channel recording from another track by its id.
inline constexpr int8_t kInputChannelSpecificTrack = 8;

/// @brief The index of @p word in @p words.
/// @return The index, or nullopt for a word the list does not hold.
template <size_t N>
[[nodiscard]] constexpr std::optional<int32_t> index_of_word(const std::string_view (&words)[N],
                                                             std::string_view word) {
	const auto* found = std::ranges::find(words, word);
	return found != std::end(words) ? std::optional<int32_t>{static_cast<int32_t>(found - std::begin(words))}
	                                : std::nullopt;
}

} // namespace deluge::project
