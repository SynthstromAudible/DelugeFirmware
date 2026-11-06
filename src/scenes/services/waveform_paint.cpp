#include "scenes/services/waveform_paint.h"

#include "ui/grid/frame.h"
#include "ui/grid/waveform.h"

#include "model/sample/waveform_scan.h"

#include "definitions_cxx.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace deluge::scenes::services {

void paint_columns(deluge::ui::grid::GridFrame& f, const WaveformRenderData& cache, int32_t centre, int32_t span) {
	const int32_t main_cols = std::min<int32_t>(f.bounds().w, kDisplayWidth);
	std::array<deluge::ui::grid::PeakCol, kDisplayWidth> peaks{};
	for (int32_t x = 0; x < main_cols; x++) {
		if (cache.colStatus[x] != COL_STATUS_INVESTIGATED) {
			continue;
		}
		int32_t min24 = 0;
		int32_t max24 = 0;
		deluge::sample::col_bar_positions(x, &cache, &min24, &max24, centre, span);
		peaks[static_cast<size_t>(x)] = {.min24 = min24, .max24 = max24, .valid = true};
	}
	// `renderOneCol`'s monochrome arm: `renderFullScreen` passes no colour, so the brightness is 128
	// (`WaveformRenderer::renderOneCol`, `gui/waveform/waveform_renderer.cpp:243-246`).
	deluge::ui::grid::waveform_full(f, peaks, deluge::ui::grid::kWaveformBarBrightnessMono, std::nullopt);
}

bool paint_full(deluge::ui::grid::GridFrame& f, const Waveforms& waveforms, const WaveformAddress& address,
                const WaveformInfo& info, deluge::sample::WaveformNav& nav) {
	// The scan can read the card, as legacy's does from inside `renderMainPads`.
	const bool scanned = waveforms.peaks(address, nav.xScroll, static_cast<uint64_t>(nav.xZoom), nav.renderData);
	paint_columns(f, nav.renderData, info.centre, info.span);
	return scanned;
}

} // namespace deluge::scenes::services
