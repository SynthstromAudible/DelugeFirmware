#pragma once
#include "model/model_stack.h"
#include <cstdlib>

class ParamCollection {
public:
	AutoParam* live;
	int expectedId;
	int swaps = 0;
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* stack) {
		if (stack->paramId != expectedId || stack->paramCollection != this) {
			std::abort();
		}
		++swaps;
		live->nodes.swapStateWith(&state->nodes);
		std::swap(live->currentValue, state->value);
	}
};
