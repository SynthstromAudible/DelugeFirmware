#include "ui/scenes/services/waveform_paint.h"

#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/waveform.h"

#include "ui/gfx/grid/pad_grid.h"

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
	for (int32_t x = 0; x < cols; x++) {
		if (const std::optional<views::waveform::Bar> bar = read.bar(x, centre, span)) {
			peaks[static_cast<size_t>(x)] = {.min24 = bar->bottom, .max24 = bar->top, .valid = true};
		}
	}
	return peaks;
}

void paint_columns(deluge::ui::widgets::grid::GridFrame& f, const views::waveform::PeakRead& read, int32_t centre,
                   int32_t span) {
	static_assert(views::waveform::kColumns == deluge::ui::gfx::grid::kDelugeGrid.main_cols);
	const int32_t main_cols = std::min<int32_t>(f.bounds().w, views::waveform::kColumns);
	// `renderOneCol`'s monochrome arm: `renderFullScreen` passes no colour, so the brightness is 128
	// (`origin/main:src/deluge/gui/waveform/waveform_renderer.cpp`, `WaveformRenderer::renderOneCol`).
	deluge::ui::widgets::grid::waveform_full(f, peak_cols(read, centre, span, main_cols),
	                                         deluge::ui::widgets::grid::kWaveformBarBrightnessMono, std::nullopt);
}

} // namespace deluge::ui::scenes::services
