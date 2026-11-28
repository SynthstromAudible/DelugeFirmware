#pragma once

/// @file
/// @brief A sample's full-screen waveform on the main pads: the read through the waveform's address, and its columns.

#include "ui/scenes/bind/ports/waveforms.h"
#include "ui/views/waveform/peaks.h"
#include "ui/widgets/grid/waveform.h"

#include <array>
#include <cstdint>
#include <optional>

namespace deluge::ui::scenes::services {

/// @brief A waveform as one frame draws it: its columns, and the centre and span its info gives after they were read.
struct WaveformFrame {
	views::waveform::PeakRead read; ///< the columns
	int32_t centre{0};              ///< the value the bars are centred on
	int32_t span{0};                ///< the value range one pad shows
};

/// @brief Read the waveform at @p address at @p at, then its info, so the bars are scaled by the range this read may
///        have widened (`origin/main:src/deluge/gui/waveform/waveform_renderer.cpp`,
///        `WaveformRenderer::renderFullScreen` reads the range after its scan).
/// @return The frame; none where nothing is loaded at @p address.
[[nodiscard]] std::optional<WaveformFrame>
read_waveform(const bind::Waveforms& waveforms, const bind::WaveformAddress& address, views::waveform::Window at);

/// @brief @p read's PEAK columns as the pad widget's columns, centred on @p centre and scaled to @p span: column
///        `x < cols` from `read.bar(x, centre, span)`, every other column invalid (nothing drawn). @p cols is clamped
///        to `kColumns`.
[[nodiscard]] std::array<deluge::ui::widgets::grid::PeakCol, views::waveform::kColumns>
peak_cols(const views::waveform::PeakRead& read, int32_t centre, int32_t span, int32_t cols);

} // namespace deluge::ui::scenes::services
