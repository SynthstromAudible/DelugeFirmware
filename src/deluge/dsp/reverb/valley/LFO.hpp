#pragma once
#include <argon.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>

class TriSawLFOBlock {
public:
	static constexpr size_t size() { return 4; }

	TriSawLFOBlock(float sampleRate = 44100.0f, argon::Neon128<float> frequency = 1.0f)
	    : _sampleRate(sampleRate), _frequency(frequency) {
		calcStepSize();
		setRevPointAll(0.5f);
	}

	argon::Neon128<float> process() {
		auto wrap = (_step > 1.0f);
		wrap.if_lane([&](int lane) { _step[lane] -= 1.0f; });
		_rising = wrap & (_step >= _revPoint);

		argon::Neon128<float> coeff;
		_rising.if_else_lane(
		    [&](int lane) { //<
			    coeff[lane] = _riseRate[lane];
		    },
		    [&](int lane) { //<
			    coeff[lane] = _fallRate[lane];
		    });

		_output = _step * coeff;

		_rising.if_else_lane( //<
		    [](int) {},
		    [&](int lane) { //<
			    _output[lane] -= _fallRate[lane];
		    });

		_step += _stepSize;
		_output *= 2.0f;
		_output -= 1.0f;
		return _output;
	}

	void setFrequency(argon::Neon128<float> frequency) {
		if ((frequency == _frequency).all()) {
			return;
		}
		_frequency = frequency;
		calcStepSize();
	}

	void setRevPointAll(float revPoint) {
		_revPoint = std::clamp(revPoint, 0.0001f, 0.999f);
		_riseRate = 1.0f / _revPoint;
		_fallRate = -1.0f / (1.0f - _revPoint);
	}

	void setSamplerate(float sampleRate) {
		_sampleRate = sampleRate;
		calcStepSize();
	}

	[[nodiscard]] argon::Neon128<float> getOutput() const { return _output; }
	[[nodiscard]] float getOutput(const int n) const { return _output[n]; }

private:
	float _sampleRate;

	argon::Neon128<float> _output;

	argon::Neon128<float> _frequency;
	argon::Neon128<float> _revPoint;
	argon::Neon128<float> _riseRate;
	argon::Neon128<float> _fallRate;
	argon::Neon128<float> _step;
	argon::Neon128<float> _stepSize;
	argon::Neon128<uint32_t> _rising{{true, true, true, true}};

	void calcStepSize() { _stepSize = _frequency / _sampleRate; }
};
