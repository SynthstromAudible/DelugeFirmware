#pragma once
#include <cmath>
#include <cstdint>

class TriSawLFO {
public:
	TriSawLFO(float sampleRate = 44100.0, float frequency = 1.0) : _sampleRate(sampleRate) {
		setFrequency(frequency);
		setRevPoint(0.5);
	}

	float process() {
		if (_step > 1.0) {
			_step -= 1.0;
			_rising = true;
		}

		if (_step >= _revPoint) {
			_rising = false;
		}

		if (_rising) {
			_output = _step * _riseRate;
		}
		else {
			_output = _step * _fallRate - _fallRate;
		}

		_step += _stepSize;
		_output *= 2.0;
		_output -= 1.0;
		return _output;
	}

	void setFrequency(float frequency) {
		if (frequency == _frequency) {
			return;
		}
		_frequency = frequency;
		calcStepSize();
	}

	void setRevPoint(float revPoint) {
		_revPoint = revPoint;
		if (_revPoint < 0.0001) {
			_revPoint = 0.0001;
		}
		if (_revPoint > 0.999) {
			_revPoint = 0.999;
		}

		_riseRate = 1.0 / _revPoint;
		_fallRate = -1.0 / (1.0 - _revPoint);
	}

	void setSamplerate(float sampleRate) {
		_sampleRate = sampleRate;
		calcStepSize();
	}

	[[nodiscard]] float getOutput() const { return _output; }

	float phase{0.0};

private:
	float _output{0.0};
	float _sampleRate;
	float _frequency = 0.0;
	float _revPoint;
	float _riseRate;
	float _fallRate;
	float _step{0.0};
	float _stepSize;
	bool _rising{true};

	constexpr void calcStepSize() { _stepSize = _frequency / _sampleRate; }
};
