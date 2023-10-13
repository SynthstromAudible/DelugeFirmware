/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "dsp/master_compressor/master_compressor.h"
#include "dsp/stereo_sample.h"

MasterCompressor::MasterCompressor() {
	compressor.setSampleRate(kSampleRate);
	compressor.initRuntime();
	compressor.setAttack(10.0);
	compressor.setRelease(100.0);
	compressor.setThresh(0.0);       //threshold (dB) 0...-69
	compressor.setRatio(1.0 / 4.00); //ratio (compression: < 1 ; expansion: > 1)
	makeup = 1.0;                    //value;
	gr = 0.0;
	wet = 1.0;
}
//with floats baseline is 60-90us
void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples, int32_t masterVolumeAdjustmentL,
                              int32_t masterVolumeAdjustmentR) {

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	if (compressor.getThresh() < -0.001) {
		do {
			//correct for input level
			float l = lshiftAndSaturate<5>(thisSample->l) / (float)ONE_Q31;
			float r = lshiftAndSaturate<5>(thisSample->r) / (float)ONE_Q31;
			float rawl = l;
			float rawr = r;
			compressor.process(l, r);
			if (thisSample == bufferEnd - 1 && fabs(rawl) > 0.00000001 && fabs(rawr) > 0.00000001) {
				gr = chunkware_simple::lin2dB(l / rawl);
				gr = std::min(gr, chunkware_simple::lin2dB(r / rawr));
			}

			l = l * makeup;
			r = r * makeup;

			if (wet < 0.9999) {
				l = rawl * (1.0 - wet) + l * wet;
				r = rawr * (1.0 - wet) + r * wet;
			}

			thisSample->l = l * ONE_Q31 / (1 << 5);
			thisSample->r = r * ONE_Q31 / (1 << 5);

		} while (++thisSample != bufferEnd);
	}
}
void MasterCompressor::setup(int32_t attack, int32_t release, int32_t threshold, int32_t ratio, int32_t makeup,
                             int32_t mix) {
	compressor.setAttack((float)attack / 100.0);
	compressor.setRelease((float)release / 100.0);
	compressor.setThresh((float)threshold / 100.0);
	compressor.setRatio(1.0 / ((float)ratio / 100.0));
	setMakeup((float)makeup / 100.0);
	wet = (float)makeup / 100.0;
}
