#pragma once

/// @file
/// @brief How long a clip's sequence may be, and the zoom that shows all of the longest one.

#include <cstdint>

namespace deluge::project {

/// The longest a sequence may be, in ticks: the largest multiple of 3 that a signed 32-bit integer holds.
inline constexpr int32_t kMaxSequenceLength = 1610612736;

/// The zoom, in ticks per pad column, at which the longest sequence fills the grid's 16 columns.
inline constexpr int32_t kMaxZoom = kMaxSequenceLength >> 4;

} // namespace deluge::project
