#pragma once

/// @file
/// @brief What a waveform shows, by address: a sound's source range's file, an audio clip's, or the sample being
/// previewed.

#include "deluge-project/clip_address.h"
#include "deluge-project/instrument_slot.h"

#include <cstdint>

namespace deluge::scenes::services {

/// @brief A waveform's address.
struct WaveformAddress {
	enum class Kind : uint8_t {
		RANGE,      ///< a sound's source @ref source, range @ref range, at @ref slot
		AUDIO_CLIP, ///< the audio clip @ref clip
		PREVIEW,    ///< the file the sample browser is previewing; no other field is read
	};
	Kind kind{Kind::RANGE};
	project::SlotAddress slot{};
	uint8_t source{0};
	uint16_t range{0};
	project::ClipAddress clip{};
	bool operator==(const WaveformAddress&) const = default;
};

} // namespace deluge::scenes::services
