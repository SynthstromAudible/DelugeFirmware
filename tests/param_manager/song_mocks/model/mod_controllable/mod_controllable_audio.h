#pragma once
#include "modulation/params/param_manager.h"

class ModControllableAudio {
public:
	ParamManagerType required_param_manager_type() { return ParamManagerType::GLOBAL; }
};
