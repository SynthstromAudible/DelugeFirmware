#pragma once
#include "modulation/automation/auto_param.h"
#include <cstring>
class ParamCollection;
struct ModelStack {};
struct ModelStackWithParamId {
	ParamCollection* paramCollection;
	int paramId;
};
struct ModelStackWithAutoParam : ModelStackWithParamId {
	AutoParam* autoParam;
};
inline constexpr auto MODEL_STACK_MAX_SIZE = sizeof(ModelStackWithAutoParam);
