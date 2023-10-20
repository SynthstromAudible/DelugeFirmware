#include "plateau.hpp"
namespace deluge::dsp::reverb {
Plateau::Plateau(const float initMaxLfoDepth, const float initMaxTimeScale) : tank{initMaxLfoDepth, initMaxTimeScale} {
	leftInputDCBlock.setCutoffFreq(20.0);
	rightInputDCBlock.setCutoffFreq(20.0);
}

void Plateau::processOne(float leftInput, float rightInput) {
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

void Plateau::clear() {
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

void Plateau::setRoomSize(float ts) {
	constexpr float minTimeScale = 0.0001;
	timeScale = ts < minTimeScale ? minTimeScale : ts;
	tank.setTimeScale(timeScale);
}

void Plateau::setPreDelay(float t) {
	settings_.pre_delay_time = t;
	preDelay.setDelayTime(t * sampleRate);
}

void Plateau::setInputFilterLowCutoffPitch(float pitch) {
	settings_.input_lpf = pitch;
	inputLowCut = 440.0f * std::pow(2.0f, pitch - 5.0f);
}

void Plateau::setInputFilterHighCutoffPitch(float pitch) {
	settings_.input_hpf = pitch;
	inputHighCut = 440.0f * std::pow(2.0f, pitch - 5.0f);
}

void Plateau::setDamping(float newDecay) {
	settings_.damping = std::clamp(newDecay, 0.f, 0.9999f);
	tank.setDecay(settings_.damping);
}

void Plateau::setWidth(const float diffusion) {
	settings_.width = diffusion;
	tank.setDiffusion(diffusion);
}

void Plateau::setTankFilterHighCutFrequency(const float pitch) {
	settings_.tank_hpf = pitch;
	float frequency = 440.0f * std::pow(2.0f, pitch - 5.0f);
	tank.setHighCutFrequency(frequency);
}

void Plateau::setTankFilterLowCutFrequency(const float pitch) {
	settings_.tank_lpf = pitch;
	float frequency = 440.0f * std::pow(2.0f, pitch - 5.0f);
	tank.setLowCutFrequency(frequency);
}
} // namespace deluge::dsp::reverb
