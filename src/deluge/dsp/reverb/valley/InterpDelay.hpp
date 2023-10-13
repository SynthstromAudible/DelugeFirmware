/**
 * @file InterpDelay2.hpp
 * @author Dale Johnson, Valley Audio Soft
 * @brief A more optimised version of the linear interpolating delay.
 */

#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

template <typename T>
T linterp(T a, T b, T f) {
	return a + f * (b - a);
}

template <typename T = float, size_t maxLength = 512>
class InterpDelay {
public:
	T input{0};
	T output{0};

	InterpDelay(uint32_t initDelayTime = 0) {
		assert(maxLength != 0);
		l = maxLength;
		setDelayTime(initDelayTime);
	}

	void process() {
		assert(w >= 0);
		assert(w < l);
		buffer[w] = input;
		int32_t r = w - t;

		if (r < 0) {
			r += l;
		}

		++w;
		if (w == l) {
			w = 0;
		}

		int32_t upperR = r - 1;
		if (upperR < 0) {
			upperR += l;
		}

		assert(r >= 0);
		assert(r < l);
		assert(upperR >= 0);
		assert(upperR < l);
		output = linterp(buffer[r], buffer[upperR], f);
	}

	T tap(int32_t i) const {

		assert(i < l);
		assert(i >= 0);

		int32_t j = w - i;
		if (j < 0) {
			j += buffer.size();
		}
		return buffer[j];
	}

	void setDelayTime(T newDelayTime) {
		if (newDelayTime >= l) {
			newDelayTime = l - 1;
		}
		if (newDelayTime < 0) {
			newDelayTime = 0;
		}
		t = static_cast<int32_t>(newDelayTime);
		f = newDelayTime - static_cast<T>(t);
	}

	void clear() {
		std::fill(buffer.begin(), buffer.end(), T(0));
		input = T(0);
		output = T(0);
	}

private:
	std::array<T, maxLength> buffer = {0};
	int32_t w = 0;
	int32_t t = 0;
	T f{0};
	int32_t l = 512;
};
