#pragma once

/// @file
/// @brief A waveform by its address: what a scene draws a sample with, without the sample.

#include "model/sample/waveform_render_data.h"
#include "scenes/services/waveform_address.h"

#include "definitions_cxx.hpp" // kDisplayWidth

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
	/// @brief Fill @p cache's per-column peaks for columns [@p x_start, @p x_end) at @p scroll and @p zoom, in
	/// samples; the cache's own scroll and zoom follow the call. It can read the card.
	/// @return False where nothing is loaded there or some cluster could not be read; the columns that were read stay
	/// valid.
	virtual bool peaks(const WaveformAddress& address, int64_t scroll, uint64_t zoom, WaveformRenderData& cache,
	                   int32_t x_start = 0, int32_t x_end = kDisplayWidth) const = 0;
};

} // namespace deluge::scenes::services
