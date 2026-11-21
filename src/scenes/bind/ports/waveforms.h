#pragma once

/// @file
/// @brief A waveform by its address: what a scene draws a sample with, without the sample.

#include "deluge-project/clip_address.h"
#include "deluge-project/instrument_slot.h"
#include "sample-view/peaks.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::bind {

/// @brief Which waveform a scene shows.
struct WaveformAddress {
	/// @brief What the address names.
	enum class Kind : uint8_t {
		RANGE,      ///< a sound's source @ref source, range @ref range, at @ref slot
		AUDIO_CLIP, ///< the audio clip @ref clip: its take while it records linearly, else its file
		PREVIEW,    ///< the file the sample browser previews; no other field is read
		SLICE,      ///< the file the slicer is open on; no other field is read
	};
	Kind kind{Kind::RANGE};      ///< what the address names
	project::SlotAddress slot{}; ///< RANGE's sound
	uint8_t source{0};           ///< RANGE's source
	uint16_t range{0};           ///< RANGE's range
	project::ClipAddress clip{}; ///< AUDIO_CLIP's clip
	bool operator==(const WaveformAddress&) const = default;
};

/// @brief What a waveform is: its length, rate and channels, and the values its bars are drawn against.
struct WaveformInfo {
	uint64_t length_samples{0};    ///< frames
	uint32_t sample_rate{0};       ///< frames per second
	uint8_t channels{0};           ///< 1 or 2
	int32_t max_peak_from_zero{0}; ///< the largest magnitude found so far
	int32_t centre{0};             ///< the value the bars are centred on
	int32_t span{0};               ///< the value range one pad shows
	bool recording{false};         ///< an audio clip's take being written, not its file
	uint64_t identity{0};          ///< which waveform this is: another file, or a take, has another
	bool operator==(const WaveformInfo&) const = default;
};

/// @brief The waveforms the app's song, preview and slicer hold, answered by the host from the samples the addresses
///        name. The host keeps the columns it has read; a scene keeps none, and asks again on a later frame while a
///        column is pending.
class Waveforms {
public:
	virtual ~Waveforms() = default;
	/// @brief What the waveform at @p address is.
	/// @return The info; none where no file is loaded there.
	[[nodiscard]] virtual std::optional<WaveformInfo> info(const WaveformAddress& address) const = 0;
	/// @brief The columns of the waveform at @p address at @p at, every one a peak, pending or past the end. It can
	///        read the card. The waveform's running range, which `info`'s centre and span come from, takes in the
	///        peaks read, so a caller that paints reads `info` after this.
	/// @return The columns; every column past the end where nothing is loaded at @p address, or where @p at's zoom is
	///         below 1.
	[[nodiscard]] virtual sample_view::PeakRead peaks(const WaveformAddress& address, sample_view::Window at) const = 0;
	/// @brief Where a voice is playing in the waveform at @p address now, in frames: the sound's newest voice on that
	///        range, or the audio clip's own (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`,
	///        `SampleMarkerEditor::graphicsRoutine`).
	/// @return The frame; none while nothing plays there.
	[[nodiscard]] virtual std::optional<int64_t> play_position(const WaveformAddress& address) const = 0;
};

} // namespace deluge::scenes::bind
