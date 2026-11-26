#pragma once

/// @file
/// @brief The markers of a sample range: where playback starts and ends, and where its loop does.

#include <cstddef>
#include <cstdint>

namespace deluge::project {

/// @brief One of a sample range's four markers, in the order they sit along the sample.
enum class SampleMarker : uint8_t {
	START,      ///< where playback starts
	LOOP_START, ///< where a loop starts
	LOOP_END,   ///< where a loop ends
	END,        ///< where playback ends
};

/// @brief How many markers a sample range has.
inline constexpr std::size_t kSampleMarkerCount = 4;

} // namespace deluge::project
