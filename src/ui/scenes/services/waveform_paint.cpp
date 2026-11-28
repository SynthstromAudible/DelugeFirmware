#include "ui/scenes/services/waveform_paint.h"

#include <algorithm>
#include <array>
#include <optional>

namespace deluge::ui::scenes::services {

std::optional<WaveformFrame> read_waveform(const bind::Waveforms& waveforms, const bind::WaveformAddress& address,
                                           views::waveform::Window at) {
	views::waveform::PeakRead read = waveforms.peaks(address, at);
	const std::optional<bind::WaveformInfo> after = waveforms.info(address);
	if (!after) {
		return std::nullopt;
	}
	return WaveformFrame{.read = read, .centre = after->centre, .span = after->span};
}

std::array<deluge::ui::widgets::grid::PeakCol, views::waveform::kColumns>
peak_cols(const views::waveform::PeakRead& read, int32_t centre, int32_t span, int32_t cols) {
	std::array<deluge::ui::widgets::grid::PeakCol, views::waveform::kColumns> peaks{};
	for (int32_t x = 0; x < std::min<int32_t>(cols, views::waveform::kColumns); x++) {
		if (const std::optional<views::waveform::Bar> bar = read.bar(x, centre, span)) {
			peaks[static_cast<size_t>(x)] = {.min24 = bar->bottom, .max24 = bar->top, .valid = true};
		}
	}
	return peaks;
}

} // namespace deluge::ui::scenes::services
