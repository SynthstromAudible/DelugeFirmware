#include "model/model_stack.h"
#include "modulation/params/param_set.h"

namespace params = deluge::modulation::params;

ModelStackWithAutoParam* ParamSet::getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation) {
	// Reject negative IDs and IDs ≥ numParams_, returning a null autoParam instead of an out-of-bounds pointer.
	if (modelStack->paramId < 0 || modelStack->paramId >= numParams_) {
		return modelStack->addAutoParam(nullptr);
	}
	return modelStack->addAutoParam(&params[modelStack->paramId]);
}
