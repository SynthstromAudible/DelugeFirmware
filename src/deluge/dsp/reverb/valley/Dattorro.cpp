#include "Dattorro.hpp"
#include <algorithm>
#include <cassert>

template <typename T>
T scale(T a, T inMin, T inMax, T outMin, T outMax) {
	return (a - inMin) / (inMax - inMin) * (outMax - outMin) + outMin;
}

Dattorro1997Tank::Dattorro1997Tank(const float initMaxLfoDepth,
                                   const float initMaxTimeScale)
    : timePadding(initMaxLfoDepth), maxTimeScale(initMaxTimeScale), fadeStep(1.0 / sampleRate) {
	rescaleTapTimes();
	setTimeScale(timeScale);
	initialiseDelaysAndApfs();
	clear();

	leftOutDCBlock.setCutoffFreq(20.0);
	rightOutDCBlock.setCutoffFreq(20.0);

	lfo1.setFrequency(lfo1Freq);
	lfo2.setFrequency(lfo2Freq);
	lfo3.setFrequency(lfo3Freq);
	lfo4.setFrequency(lfo4Freq);

	lfo2.phase = 0.25;
	lfo3.phase = 0.5;
	lfo4.phase = 0.75;

	lfo1.setRevPoint(0.5);
	lfo2.setRevPoint(0.5);
	lfo3.setRevPoint(0.5);
	lfo4.setRevPoint(0.5);
}

void Dattorro1997Tank::process(const float leftIn, const float rightIn, float* leftOut, float* rightOut) {
	tickApfModulation();

	decay = frozen ? 1.0f : decayParam;

	leftSum += leftIn;
	rightSum += rightIn;

	leftApf1.input = leftSum;
	leftDelay1.input = leftApf1.process();
	leftDelay1.process();
	leftHighCutFilter.input = leftDelay1.output;
	leftLowCutFilter.input = leftHighCutFilter.process();
	leftApf2.input = (leftDelay1.output * (1.0f - fade) + leftLowCutFilter.process() * fade) * decay;
	leftDelay2.input = leftApf2.process();
	leftDelay2.process();

	rightApf1.input = rightSum;
	rightDelay1.input = rightApf1.process();
	rightDelay1.process();
	rightHighCutFilter.input = rightDelay1.output;
	rightLowCutFilter.input = rightHighCutFilter.process();
	rightApf2.input = (rightDelay1.output * (1.0f - fade) + rightLowCutFilter.process() * fade) * decay;
	rightDelay2.input = rightApf2.process();
	rightDelay2.process();

	rightSum = leftDelay2.output * decay;
	leftSum = rightDelay2.output * decay;

	leftOutDCBlock.input = leftApf1.output;
	leftOutDCBlock.input += leftDelay1.tap(scaledOutputTaps[L_DELAY_1_L_TAP_1]);
	leftOutDCBlock.input += leftDelay1.tap(scaledOutputTaps[L_DELAY_1_L_TAP_2]);
	leftOutDCBlock.input -= leftApf2.delay.tap(scaledOutputTaps[L_APF_2_L_TAP]);
	leftOutDCBlock.input += leftDelay2.tap(scaledOutputTaps[L_DELAY_2_L_TAP]);
	leftOutDCBlock.input -= rightDelay1.tap(scaledOutputTaps[R_DELAY_1_L_TAP]);
	leftOutDCBlock.input -= rightApf2.delay.tap(scaledOutputTaps[R_APF_2_L_TAP]);
	leftOutDCBlock.input -= rightDelay2.tap(scaledOutputTaps[R_DELAY_2_L_TAP]);

	rightOutDCBlock.input = rightApf1.output;
	rightOutDCBlock.input += rightDelay1.tap(scaledOutputTaps[R_DELAY_1_R_TAP_1]);
	rightOutDCBlock.input += rightDelay1.tap(scaledOutputTaps[R_DELAY_1_R_TAP_2]);
	rightOutDCBlock.input -= rightApf2.delay.tap(scaledOutputTaps[R_APF_2_R_TAP]);
	rightOutDCBlock.input += rightDelay2.tap(scaledOutputTaps[R_DELAY_2_R_TAP]);
	rightOutDCBlock.input -= leftDelay1.tap(scaledOutputTaps[L_DELAY_1_R_TAP]);
	rightOutDCBlock.input -= leftApf2.delay.tap(scaledOutputTaps[L_APF_2_R_TAP]);
	rightOutDCBlock.input -= leftDelay2.tap(scaledOutputTaps[L_DELAY_2_R_TAP]);

	*leftOut = leftOutDCBlock.process() * 0.5f;
	*rightOut = rightOutDCBlock.process() * 0.5f;

	fade += fadeStep * fadeDir;
	fade = (fade < 0.0f) ? 0.0f : ((fade > 1.0f) ? 1.0f : fade);

	assert(fade >= 0.0f);
	assert(fade <= 1.0f);
}

void Dattorro1997Tank::freeze(bool freezeFlag) {
	frozen = freezeFlag;
	if (frozen) {
		fadeDir = -1.0;
		decay = 1.0;
	}
	else {
		fadeDir = 1.0;
		decay = decayParam;
	}
}


void Dattorro1997Tank::setTimeScale(const float newTimeScale) {
	timeScale = newTimeScale;
	timeScale = timeScale < minTimeScale ? minTimeScale : timeScale;

	rescaleApfAndDelayTimes();
}

void Dattorro1997Tank::setDecay(const float newDecay) {
	decayParam = (float)(newDecay > 1.0 ? 1.0 : (newDecay < 0.0 ? 0.0 : newDecay));
}

void Dattorro1997Tank::setModSpeed(const float newModSpeed) {
	lfo1.setFrequency(lfo1Freq * newModSpeed);
	lfo2.setFrequency(lfo2Freq * newModSpeed);
	lfo3.setFrequency(lfo3Freq * newModSpeed);
	lfo4.setFrequency(lfo4Freq * newModSpeed);
}

void Dattorro1997Tank::setModDepth(const float newModDepth) {
	modDepth = newModDepth;
	lfoExcursion = newModDepth * lfoMaxExcursion * sampleRateScale;
}

void Dattorro1997Tank::setModShape(const float shape) {
	lfo1.setRevPoint(shape);
	lfo2.setRevPoint(shape);
	lfo3.setRevPoint(shape);
	lfo4.setRevPoint(shape);
}

void Dattorro1997Tank::setHighCutFrequency(const float frequency) {
	leftHighCutFilter.setCutoffFreq(frequency);
	rightHighCutFilter.setCutoffFreq(frequency);
}

void Dattorro1997Tank::setLowCutFrequency(const float frequency) {
	leftLowCutFilter.setCutoffFreq(frequency);
	rightLowCutFilter.setCutoffFreq(frequency);
}

void Dattorro1997Tank::setDiffusion(const float diffusion) {
	assert(diffusion >= 0.0 && diffusion <= 10.0);

	auto diffusion1 = scale<float>(diffusion, 0.0, 10.0, 0.0, maxDiffusion1);
	auto diffusion2 = scale<float>(diffusion, 0.0, 10.0, 0.0, maxDiffusion2);

	leftApf1.setGain(-diffusion1);
	leftApf2.setGain(diffusion2);
	rightApf1.setGain(-diffusion1);
	rightApf2.setGain(diffusion2);
}

void Dattorro1997Tank::clear() {
	leftApf1.clear();
	leftDelay1.clear();
	leftHighCutFilter.clear();
	leftLowCutFilter.clear();
	leftApf2.clear();
	leftDelay2.clear();

	rightApf1.clear();
	rightDelay1.clear();
	rightHighCutFilter.clear();
	rightLowCutFilter.clear();
	rightApf2.clear();
	rightDelay2.clear();

	leftOutDCBlock.clear();
	rightOutDCBlock.clear();

	leftSum = 0.0;
	rightSum = 0.0;
}

void Dattorro1997Tank::initialiseDelaysAndApfs() {
	auto maxScaledOutputTap = *std::max_element(scaledOutputTaps.begin(), scaledOutputTaps.end());
	auto calcMaxTime = [&](float delayTime) -> int32_t {
		return (int32_t)(sampleRateScale * (delayTime * maxTimeScale + maxScaledOutputTap + timePadding));
	};

	const int32_t kLeftApf1MaxTime = calcMaxTime(leftApf1Time);
	const int32_t kLeftDelay1MaxTime = calcMaxTime(leftDelay1Time);
	const int32_t kLeftApf2MaxTime = calcMaxTime(leftApf2Time);
	const int32_t kLeftDelay2MaxTime = calcMaxTime(leftDelay2Time);
	const int32_t kRightApf1MaxTime = calcMaxTime(rightApf1Time);
	const int32_t kRightDelay1MaxTime = calcMaxTime(rightDelay1Time);
	const int32_t kRightApf2MaxTime = calcMaxTime(rightApf2Time);
	const int32_t kRightDelay2MaxTime = calcMaxTime(rightDelay2Time);

	leftApf1 = AllpassFilter<float>(kLeftApf1MaxTime);
	leftDelay1 = InterpDelay<float>(kLeftDelay1MaxTime);
	leftApf2 = AllpassFilter<float>(kLeftApf2MaxTime);
	leftDelay2 = InterpDelay<float>(kLeftDelay2MaxTime);
	rightApf1 = AllpassFilter<float>(kRightApf1MaxTime);
	rightDelay1 = InterpDelay<float>(kRightDelay1MaxTime);
	rightApf2 = AllpassFilter<float>(kRightApf2MaxTime);
	rightDelay2 = InterpDelay<float>(kRightDelay2MaxTime);
}

void Dattorro1997Tank::tickApfModulation() {
	leftApf1.delay.setDelayTime(lfo1.process() * lfoExcursion + scaledLeftApf1Time);
	leftApf2.delay.setDelayTime(lfo2.process() * lfoExcursion + scaledLeftApf2Time);
	rightApf1.delay.setDelayTime(lfo3.process() * lfoExcursion + scaledRightApf1Time);
	rightApf2.delay.setDelayTime(lfo4.process() * lfoExcursion + scaledRightApf2Time);
}

void Dattorro1997Tank::rescaleApfAndDelayTimes() {
	float scaleFactor = timeScale * sampleRateScale;

	scaledLeftApf1Time = leftApf1Time * scaleFactor;
	scaledLeftDelay1Time = leftDelay1Time * scaleFactor;
	scaledLeftApf2Time = leftApf2Time * scaleFactor;
	scaledLeftDelay2Time = leftDelay2Time * scaleFactor;

	scaledRightApf1Time = rightApf1Time * scaleFactor;
	scaledRightDelay1Time = rightDelay1Time * scaleFactor;
	scaledRightApf2Time = rightApf2Time * scaleFactor;
	scaledRightDelay2Time = rightDelay2Time * scaleFactor;

	leftDelay1.setDelayTime(scaledLeftDelay1Time);
	leftDelay2.setDelayTime(scaledLeftDelay2Time);
	rightDelay1.setDelayTime(scaledRightDelay1Time);
	rightDelay2.setDelayTime(scaledRightDelay2Time);
}

void Dattorro1997Tank::rescaleTapTimes() {
	for (size_t i = 0; i < scaledOutputTaps.size(); ++i) {
		scaledOutputTaps[i] = (int32_t)((float)kOutputTaps[i] * sampleRateScale);
	}
}

Dattorro::Dattorro(const float initMaxLfoDepth, const float initMaxTimeScale)
    : tank(initMaxLfoDepth, initMaxTimeScale) {

	preDelay = decltype(preDelay)(0);

	inputLpf = OnePoleLPFilter(22000.0);
	inputHpf = OnePoleHPFilter(0.0);

	inApf1 = AllpassFilter<float>(dattorroScale(8 * kInApf1Time), dattorroScale(kInApf1Time), inputDiffusion1);
	inApf2 = AllpassFilter<float>(dattorroScale(8 * kInApf2Time), dattorroScale(kInApf2Time), inputDiffusion1);
	inApf3 = AllpassFilter<float>(dattorroScale(8 * kInApf3Time), dattorroScale(kInApf3Time), inputDiffusion2);
	inApf4 = AllpassFilter<float>(dattorroScale(8 * kInApf4Time), dattorroScale(kInApf4Time), inputDiffusion2);

	leftInputDCBlock.setCutoffFreq(20.0);
	rightInputDCBlock.setCutoffFreq(20.0);
}

void Dattorro::process(float leftInput, float rightInput) {
	leftInputDCBlock.input = leftInput;
	rightInputDCBlock.input = rightInput;
	inputLpf.setCutoffFreq(inputHighCut);
	inputHpf.setCutoffFreq(inputLowCut);
	inputLpf.input = leftInputDCBlock.process() + rightInputDCBlock.process();
	inputHpf.input = inputLpf.process();
	inputHpf.process();
	preDelay.input = inputHpf.output;
	preDelay.process();
	inApf1.input = preDelay.output;
	inApf2.input = inApf1.process();
	inApf3.input = inApf2.process();
	inApf4.input = inApf3.process();
	tankFeed = preDelay.output * (1.0 - diffuseInput) + inApf4.process() * diffuseInput;

	tank.process(tankFeed, tankFeed, &leftOut, &rightOut);
}

void Dattorro::clear() {
	leftInputDCBlock.clear();
	rightInputDCBlock.clear();

	inputLpf.clear();
	inputHpf.clear();
	preDelay.clear();
	inApf1.clear();
	inApf2.clear();
	inApf3.clear();
	inApf4.clear();

	tank.clear();
}

void Dattorro::setTimeScale(float timeScale) {
	constexpr float minTimeScale = 0.0001;
	timeScale = timeScale < minTimeScale ? minTimeScale : timeScale;
	tank.setTimeScale(timeScale);
}

void Dattorro::setPreDelay(float t) {
	preDelayTime = t;
	preDelay.setDelayTime(preDelayTime * sampleRate);
}

void Dattorro::freeze(bool freezeFlag) {
	tank.freeze(freezeFlag);
}

void Dattorro::setInputFilterLowCutoffPitch(float pitch) {
	inputLowCut = 440.0 * std::pow(2.0, pitch - 5.0);
}

void Dattorro::setInputFilterHighCutoffPitch(float pitch) {
	inputHighCut = 440.0 * std::pow(2.0, pitch - 5.0);
}

void Dattorro::enableInputDiffusion(bool enable) {
	diffuseInput = enable ? 1.0 : 0.0;
}

void Dattorro::setDecay(float newDecay) {
	decay = newDecay;
	assert(decay <= 1.0);
	tank.setDecay(decay);
}

void Dattorro::setTankDiffusion(const float diffusion) {
	tank.setDiffusion(diffusion);
}

void Dattorro::setTankFilterHighCutFrequency(const float pitch) {
	auto frequency = 440.0 * std::pow(2.0, pitch - 5.0);
	tank.setHighCutFrequency(frequency);
}

void Dattorro::setTankFilterLowCutFrequency(const float pitch) {
	auto frequency = 440.0 * std::pow(2.0, pitch - 5.0);
	tank.setLowCutFrequency(frequency);
}

void Dattorro::setTankModSpeed(const float modSpeed) {
	tank.setModSpeed(modSpeed);
}

void Dattorro::setTankModDepth(const float modDepth) {
	tank.setModDepth(modDepth);
}

void Dattorro::setTankModShape(const float modShape) {
	tank.setModShape(modShape);
}

float Dattorro::getLeftOutput() const {
	return leftOut;
}

float Dattorro::getRightOutput() const {
	return rightOut;
}

float Dattorro::dattorroScale(float delayTime) {
	return delayTime * dattorroScaleFactor;
}
