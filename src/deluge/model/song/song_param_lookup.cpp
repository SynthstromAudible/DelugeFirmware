#include "model/model_stack.h"
#include "model/song/song.h"

namespace params = deluge::modulation::params;
using params::kNoParamID;

ModelStackWithAutoParam* Song::getModelStackWithParam(ModelStackWithThreeMainThings* modelStack, int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (modelStack) {
		modelStackWithParam = modelStack->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}
