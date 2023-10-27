/**
 * @file AllpassFilter.hpp
 * @author Dale Johnson
 * @date ...uhhh
 */

#pragma once
#include "InterpDelay.hpp"
#include <cassert>

template <class T, size_t maxDelay>
class AllpassFilter {
public:
	AllpassFilter() { clear(); }

	AllpassFilter(long initDelay, T gain = 0) : delay(initDelay), _gain(gain) { clear(); }

	void init(long initDelay, T gain = 0) {
		delay.init(initDelay);
		_gain = gain;
		clear();
	}

	[[gnu::always_inline]] T inline process() {
		_inSum = input + delay.output * _gain;
		output = delay.output + _inSum * _gain * -1;
		delay.input = _inSum;
		delay.process();
		return output;
	}

	void clear() {
		input = 0;
		output = 0;
		_inSum = 0;
		_outSum = 0;
		delay.clear();
	}

	void setGain(const T newGain) {
		assert(newGain >= -1.0);
		assert(newGain <= 1.0);

		_gain = newGain;
	}

	T input;
	T output;
	InterpDelay<T, maxDelay> delay;

private:
	T _gain{0};
	T _inSum;
	T _outSum;
};

template <class T, size_t maxLength>
class NestedAllPassType1 {
public:
	NestedAllPassType1() { clear(); }

	NestedAllPassType1(long maxDelay, long delayTime1, long delayTime2)
	    : delay1(InterpDelay<T, maxLength>(maxDelay, delayTime1)),
	      delay2(InterpDelay<T, maxLength>(maxDelay, delayTime2)) {
		clear();
	}

	T inline process() {
		_inSum1 = input + delay1.output * gain1;
		_inSum2 = _inSum1 + delay2.output * gain2;
		delay2.input = _inSum2;
		delay1.input = delay2.output * decay2 + _inSum2 * -gain2;
		output = delay1.output * decay1 + _inSum1 * -gain1;
		delay1.process();
		delay2.process();
		return output;
	}

	void clear() {
		input = 0;
		output = 0;
		_inSum1 = 0;
		_inSum2 = 0;
		delay1.clear();
		delay2.clear();
	}

	T input;
	T gain1{0};
	T gain2{0};
	T output;
	T decay1{0};
	T decay2{0};
	InterpDelay<T, maxLength> delay1;
	InterpDelay<T, maxLength> delay2;

private:
	T _inSum1, _inSum2;
};
