#pragma once

/// @file
/// @brief A waveform by its address: what a scene draws a sample with, without the sample.

#include "sample-view/peaks.h"
#include "scenes/services/waveform_address.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::services {

/// @brief What a waveform is: its length, rate and channels, and the normalisation its bars are drawn against.
struct WaveformInfo {
	uint64_t length_samples{0};
	uint32_t sample_rate{0};
	uint8_t channels{0};
	int32_t max_peak_from_zero{0};
	int32_t centre{0};     ///< the value the bars are centred on
	int32_t span{0};       ///< the value range the bars are scaled to
	bool recording{false}; ///< an audio clip's take being recorded, not its file
	uint64_t identity{0};  ///< which waveform this is: another file, or a take, has another
};

/// @brief The waveforms of the current song, answered by the host from the samples the addresses name.
class Waveforms {
public:
	virtual ~Waveforms() = default;
	/// @brief What the waveform at @p address is; nullopt where no file is loaded there. An audio clip recording
	/// linearly answers for the take being written, as legacy's audio clip view draws it.
	[[nodiscard]] virtual std::optional<WaveformInfo> info(const WaveformAddress& address) const = 0;
	/// @brief The columns of the waveform at @p address at @p at, every one a peak, pending or past the end. It can
	///        read the card. The waveform's running range, which `info`'s centre and span come from, takes in the
	///        peaks read, so a caller that paints reads `info` after this.
	/// @return The columns; every column past the end where nothing is loaded at @p address.
	[[nodiscard]] virtual sample_view::PeakRead peaks(const WaveformAddress& address, sample_view::Window at) const = 0;
	/// @brief Where in the waveform at @p address a voice is playing now, in samples: the sound's newest voice on that
	/// range, or the audio clip's own; nullopt while nothing plays there (`SampleMarkerEditor::graphicsRoutine`).
	[[nodiscard]] virtual std::optional<int64_t> play_position(const WaveformAddress& address) const = 0;
};

} // namespace deluge::scenes::services
