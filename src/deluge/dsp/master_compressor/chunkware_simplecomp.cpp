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

#include "dsp/master_compressor/chunkware_simplecomp.h"
#include "dsp/stereo_sample.h"

namespace chunkware_simple {
//-------------------------------------------------------------
// envelope detector
//-------------------------------------------------------------
EnvelopeDetector::EnvelopeDetector(float timeConstant, float sampleRate) {
	assert(sampleRate > 0.0);
	assert(timeConstant > 0.0);
	sampleRate_ = sampleRate;
	timeConstant_ = timeConstant;
	setCoef();
}
//-------------------------------------------------------------
void EnvelopeDetector::setTc(float timeConstant) {
	assert(timeConstant > 0.0);
	timeConstant_ = timeConstant;
	setCoef();
}
//-------------------------------------------------------------
void EnvelopeDetector::setSampleRate(float sampleRate) {
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
AttRelEnvelope::AttRelEnvelope(float att_ms, float rel_ms, float sampleRate)
    : attackEnvelope_(att_ms, sampleRate), releaseEnvelope_(rel_ms, sampleRate) {
}
//-------------------------------------------------------------
void AttRelEnvelope::setAttack(float ms) {
	attackEnvelope_.setTc(ms);
}
//-------------------------------------------------------------
void AttRelEnvelope::setRelease(float ms) {
	releaseEnvelope_.setTc(ms);
}
//-------------------------------------------------------------
void AttRelEnvelope::setSampleRate(float sampleRate) {
	attackEnvelope_.setSampleRate(sampleRate);
	releaseEnvelope_.setSampleRate(sampleRate);
}

//-------------------------------------------------------------
// simple compressor
//-------------------------------------------------------------
SimpleComp::SimpleComp() : AttRelEnvelope(10.0, 100.0), threshdB_(0.0), ratio_(1.0), envdB_(DC_OFFSET) {
}
//-------------------------------------------------------------
void SimpleComp::setThresh(float dB) {
	threshdB_ = dB;
}
//-------------------------------------------------------------
void SimpleComp::setRatio(float ratio) {
	assert(ratio > 0.0);
	ratio_ = ratio;
}
//-------------------------------------------------------------
void SimpleComp::initRuntime(void) {
	envdB_ = DC_OFFSET;
}

} // end namespace chunkware_simple
