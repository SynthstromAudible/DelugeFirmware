#pragma once

/// @file
/// @brief A sample's full-screen waveform on the main pads: legacy's `WaveformRenderer::renderFullScreen`, as its two
/// halves -- the scan, through the waveform's address, and the paint.

#include "scenes/services/waveform_address.h"
#include "scenes/services/waveforms.h"

#include "model/sample/waveform_navigator.h"
#include "model/sample/waveform_render_data.h"

#include <cstdint>

namespace deluge::ui::grid {
class GridFrame;
}

namespace deluge::scenes::services {

/// @brief Paint @p cache's investigated columns as full-height monochrome bars across @p f's main columns (at most
/// `kDisplayWidth`), centred on @p centre and scaled to @p span. A column the scan could not read stays black.
void paint_columns(deluge::ui::grid::GridFrame& f, const WaveformRenderData& cache, int32_t centre, int32_t span);

/// @brief Scan the waveform at @p address at @p nav's scroll and zoom into its cache, then paint it with
/// @ref paint_columns against @p info's centre and span.
/// @return The scan's result: false where some cluster could not be read (those columns paint black).
bool paint_full(deluge::ui::grid::GridFrame& f, const Waveforms& waveforms, const WaveformAddress& address,
                const WaveformInfo& info, deluge::sample::WaveformNav& nav);

} // namespace deluge::scenes::services
