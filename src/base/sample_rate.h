#pragma once

/// @file
/// @brief The audio engine's fixed sample rate.

#include <cstdint>

namespace deluge::base {

/// The rate the engine renders and records at, in frames per second.
inline constexpr uint32_t kSampleRate = 44100;

} // namespace deluge::base
