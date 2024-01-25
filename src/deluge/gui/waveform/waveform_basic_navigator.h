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
#include "gui/waveform/waveform_render_data.h"
#include <cstdint>

class Sample;
struct MarkerColumn;
class SampleHolder;

// This class supports basic navigation for samples not tied to a timeline - i.e. not AudioClips. Navigation is done and
// thought about at the individual-sample level. This is used (but not inherited) by SampleMarkerEditor and
// SampleBrowser

class WaveformBasicNavigator {
public:
	WaveformBasicNavigator();
	void opened(SampleHolder* range = NULL);
	int32_t getMaxZoom();
	bool isZoomedIn();

	bool zoom(int32_t offset, bool shouldAllowExtraScrollRight = false, MarkerColumn* cols = NULL,
	          MarkerType markerType = MarkerType::NONE);
	bool scroll(int32_t offset, bool shouldAllowExtraScrollRight = false, MarkerColumn* cols = NULL);

	Sample* sample;

	int64_t xZoom;
	int64_t xScroll;

	WaveformRenderData renderData;
};

extern WaveformBasicNavigator waveformBasicNavigator;
