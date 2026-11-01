/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/sample/sample_controls.h"
#include "definitions_cxx.hpp"
#include "processing/engines/audio_engine.h"
#include "synth/sources/interpolation_buffer_size.h"

SampleControls::SampleControls() {
	interpolationMode = InterpolationMode::SMOOTH;
	pitchAndSpeedAreIndependent = false;
	reversed = false;
	invertReversed = false;
}

int32_t SampleControls::getInterpolationBufferSize(int32_t phaseIncrement) {
	return getInterpolationBufferSize(interpolationMode, phaseIncrement);
}

int32_t SampleControls::getInterpolationBufferSize(InterpolationMode interpolationMode, int32_t phaseIncrement) {
	return deluge::synth::interpolation_buffer_size(interpolationMode, phaseIncrement, AudioEngine::cpuDireness);
}

bool SampleControls::isCurrentlyReversed() const {
	return invertReversed ? !reversed : reversed;
}
