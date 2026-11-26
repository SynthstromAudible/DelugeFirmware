/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "ui/views/waveform/peaks.h"
#include <cstdint>
#include <optional>

class Sample;
class MultisampleRange;
class SampleRecorder;

// This class provides low-level rendering functions for the waveform only

class WaveformRenderer {
public:
	WaveformRenderer();

	bool renderFullScreen(Sample* sample, uint64_t xScroll, uint64_t xZoom,
	                      RGB thisImage[][kDisplayWidth + kSideBarWidth], SampleRecorder* recorder = nullptr,
	                      std::optional<RGB> rgb = std::nullopt, bool reversed = false, int32_t xEnd = kDisplayWidth);
	bool renderAsSingleRow(Sample* sample, int64_t xScroll, uint64_t xZoom, RGB* thisImage, SampleRecorder* recorder,
	                       RGB rgb, bool reversed, int32_t xStart, int32_t xEnd);
	void renderOneCol(Sample* sample, int32_t xDisplay, RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                  const deluge::ui::views::waveform::PeakRead& read, bool reversed = false,
	                  std::optional<RGB> rgb = std::nullopt);
	void renderOneColForCollapseAnimation(int32_t xDisplay, int32_t xDisplayOutput, int32_t maxPeakFromZero,
	                                      int32_t progress, RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                                      const deluge::ui::views::waveform::PeakRead& read, std::optional<RGB> rgb,
	                                      bool reversed, int32_t valueCentrePoint, int32_t valueSpan);
	void renderOneColForCollapseAnimationZoomedOut(int32_t xDisplayWaveformLeftEdge, int32_t xDisplayWaveformRightEdge,
	                                               int32_t xDisplayOutput, int32_t maxPeakFromZero, int32_t progress,
	                                               RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                                               const deluge::ui::views::waveform::PeakRead& read,
	                                               std::optional<RGB> rgb, bool reversed, int32_t valueCentrePoint,
	                                               int32_t valueSpan);
	int8_t collapseAnimationToWhichRow{};

private:
	void drawColBar(int32_t xDisplay, int32_t min24, int32_t max24, RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                int32_t brightness = 128, std::optional<RGB> rgb = std::nullopt);
	void renderOneColForCollapseAnimationInterpolation(int32_t xDisplayOutput, int32_t min24, int32_t max24,
	                                                   int32_t singleSquareBrightness, int32_t progress,
	                                                   RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                                                   std::optional<RGB> rgb);
};

extern WaveformRenderer waveformRenderer;
