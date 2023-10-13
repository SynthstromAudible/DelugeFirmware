#include "OnePoleFilters.hpp"
#include <cassert>

OnePoleLPFilter::OnePoleLPFilter(float cutoffFreq) {
	setCutoffFreq(cutoffFreq);
}

float OnePoleLPFilter::process() {
	_z = _a * input + _z * _b;
	output = _z;
	return output;
}

void OnePoleLPFilter::clear() {
	input = 0.0;
	_z = 0.0;
	output = 0.0;
}

void OnePoleLPFilter::setCutoffFreq(float cutoffFreq) {
	if (cutoffFreq == _cutoffFreq) {
		return;
	}

	assert(cutoffFreq > 0.0);
	assert(cutoffFreq <= _maxCutoffFreq);

	_cutoffFreq = cutoffFreq;
	_b = expf(-_2M_PI * _cutoffFreq * _1_sampleRate);
	_a = 1.0 - _b;
}

float OnePoleLPFilter::getMaxCutoffFreq() const {
	return _maxCutoffFreq;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

OnePoleHPFilter::OnePoleHPFilter(float initCutoffFreq) {
	setCutoffFreq(initCutoffFreq);
	clear();
}

float OnePoleHPFilter::process() {
	_x0 = input;
	_y0 = _a0 * _x0 + _a1 * _x1 + _b1 * _y1;
	_y1 = _y0;
	_x1 = _x0;
	output = _y0;
	return _y0;
}

void OnePoleHPFilter::clear() {
	input = 0.0;
	output = 0.0;
	_x0 = 0.0;
	_x1 = 0.0;
	_y0 = 0.0;
	_y1 = 0.0;
}

void OnePoleHPFilter::setCutoffFreq(float cutoffFreq) {
	if (cutoffFreq == _cutoffFreq) {
		return;
	}

	assert(cutoffFreq > 0.0);
	assert(cutoffFreq <= _maxCutoffFreq);

	_cutoffFreq = cutoffFreq;
	_b1 = expf(-_2M_PI * _cutoffFreq * _1_sampleRate);
	_a0 = (1.0 + _b1) / 2.0;
	_a1 = -_a0;
}

DCBlocker::DCBlocker(float cutoffFreq) {
	setCutoffFreq(cutoffFreq);
}

float DCBlocker::process(float input) {
	output = input - _z + _b * output;
	_z = input;
	return output;
}

void DCBlocker::clear() {
	_z = 0.0;
	output = 0.0;
}

void DCBlocker::setCutoffFreq(float cutoffFreq) {
	_cutoffFreq = cutoffFreq;
	_b = 0.999;
}
