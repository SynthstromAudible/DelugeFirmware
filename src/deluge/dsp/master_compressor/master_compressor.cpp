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
	compressor.setSampleRate(44100);
	compressor.initRuntime();
	compressor.setAttack(10.0);
	compressor.setRelease(100.0);
	compressor.setThresh(0.0);       //threshold (dB) 0...-69
	compressor.setRatio(1.0 / 4.00); //ratio (compression: < 1 ; expansion: > 1)
	makeup = 1.0;                    //value;
	gr = 0.0;
	wet = 1.0;
}

void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples, int32_t masterVolumeAdjustmentL,
                              int32_t masterVolumeAdjustmentR) {

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	if (compressor.getThresh() < -0.001) {
		double adjustmentL = (masterVolumeAdjustmentL) / 4294967296.0; //  *2.0 is <<1 from multiply_32x32_rshift32
		double adjustmentR = (masterVolumeAdjustmentR) / 4294967296.0;
		if (adjustmentL < 0.000001)
			adjustmentL = 0.000001;
		if (adjustmentR < 0.000001)
			adjustmentR = 0.000001;
		do {
			double l = thisSample->l / (double)ONE_Q31 / adjustmentL;
			double r = thisSample->r / (double)ONE_Q31 / adjustmentR;
			double rawl = l;
			double rawr = r;
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

			thisSample->l = l * ONE_Q31;
			thisSample->r = r * ONE_Q31;
			thisSample->l = multiply_32x32_rshift32(thisSample->l, masterVolumeAdjustmentL);
			thisSample->r = multiply_32x32_rshift32(thisSample->r, masterVolumeAdjustmentR);

		} while (++thisSample != bufferEnd);
	}
}

namespace chunkware_simple {
//-------------------------------------------------------------
// envelope detector
//-------------------------------------------------------------
EnvelopeDetector::EnvelopeDetector(double timeConstant, double sampleRate) {
	assert(sampleRate > 0.0);
	assert(timeConstant > 0.0);
	sampleRate_ = sampleRate;
	timeConstant_ = timeConstant;
	setCoef();
}
//-------------------------------------------------------------
void EnvelopeDetector::setTc(double timeConstant) {
	assert(timeConstant > 0.0);
	timeConstant_ = timeConstant;
	setCoef();
}
//-------------------------------------------------------------
void EnvelopeDetector::setSampleRate(double sampleRate) {
	assert(sampleRate > 0.0);
	sampleRate_ = sampleRate;
	setCoef();
}
//-------------------------------------------------------------
void EnvelopeDetector::setCoef(void) {
	nSamplesInverse_ = exp(-1000.0 / (timeConstant_ * sampleRate_));
}

//-------------------------------------------------------------
// attack/release envelope
//-------------------------------------------------------------
AttRelEnvelope::AttRelEnvelope(double att_ms, double rel_ms, double sampleRate)
    : attackEnvelope_(att_ms, sampleRate), releaseEnvelope_(rel_ms, sampleRate) {
}
//-------------------------------------------------------------
void AttRelEnvelope::setAttack(double ms) {
	attackEnvelope_.setTc(ms);
}
//-------------------------------------------------------------
void AttRelEnvelope::setRelease(double ms) {
	releaseEnvelope_.setTc(ms);
}
//-------------------------------------------------------------
void AttRelEnvelope::setSampleRate(double sampleRate) {
	attackEnvelope_.setSampleRate(sampleRate);
	releaseEnvelope_.setSampleRate(sampleRate);
}

//-------------------------------------------------------------
// simple compressor
//-------------------------------------------------------------
SimpleComp::SimpleComp() : AttRelEnvelope(10.0, 100.0), threshdB_(0.0), ratio_(1.0), envdB_(DC_OFFSET) {
}
//-------------------------------------------------------------
void SimpleComp::setThresh(double dB) {
	threshdB_ = dB;
}
//-------------------------------------------------------------
void SimpleComp::setRatio(double ratio) {
	assert(ratio > 0.0);
	ratio_ = ratio;
}
//-------------------------------------------------------------
void SimpleComp::initRuntime(void) {
	envdB_ = DC_OFFSET;
}

} // end namespace chunkware_simple
