#pragma once
#include <algorithm>
#include <memory>
#include <utility>

class ParamNodeVector {
public:
	std::unique_ptr<int[]> buffer;
	int size = 0;
	void swapStateWith(ParamNodeVector* other) {
		buffer.swap(other->buffer);
		std::swap(size, other->size);
	}
	void cloneFrom(ParamNodeVector* other) {
		size = other->size;
		if (size) {
			buffer = std::make_unique<int[]>(size);
			std::copy_n(other->buffer.get(), size, buffer.get());
		}
	}
};

struct AutoParamState {
	ParamNodeVector nodes;
	int value = 0;
};

struct AutoParam {
	ParamNodeVector nodes;
	int currentValue = 0;
};
