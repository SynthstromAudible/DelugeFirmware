#include "scenes/services/waveform_paint.h"

#include "ui/grid/frame.h"
#include "ui/grid/waveform.h"

#include "gfx/grid/pad_grid.h"

#include <algorithm>
#include <array>
#include <optional>

namespace deluge::scenes::services {

std::optional<WaveformFrame> read_waveform(const Waveforms& waveforms, const WaveformAddress& address,
                                           sample_view::Window at) {
	sample_view::PeakRead read = waveforms.peaks(address, at);
	const std::optional<WaveformInfo> after = waveforms.info(address);
	if (!after) {
		return std::nullopt;
	}
	return WaveformFrame{.read = read, .centre = after->centre, .span = after->span};
}

void paint_columns(deluge::ui::grid::GridFrame& f, const sample_view::PeakRead& read, int32_t centre, int32_t span) {
	static_assert(sample_view::kColumns == deluge::gfx::grid::kDelugeGrid.main_cols);
	const int32_t main_cols = std::min<int32_t>(f.bounds().w, sample_view::kColumns);
	std::array<deluge::ui::grid::PeakCol, sample_view::kColumns> peaks{};
	for (int32_t x = 0; x < main_cols; x++) {
		if (const std::optional<sample_view::Bar> bar = read.bar(x, centre, span)) {
			peaks[static_cast<size_t>(x)] = {.min24 = bar->bottom, .max24 = bar->top, .valid = true};
		}
	}
	// `renderOneCol`'s monochrome arm: `renderFullScreen` passes no colour, so the brightness is 128
	// (`origin/main:src/deluge/gui/waveform/waveform_renderer.cpp`, `WaveformRenderer::renderOneCol`).
	deluge::ui::grid::waveform_full(f, peaks, deluge::ui::grid::kWaveformBarBrightnessMono, std::nullopt);
}

} // namespace deluge::scenes::services
