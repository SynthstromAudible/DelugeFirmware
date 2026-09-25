#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "processing/audio_output.h"

namespace params = deluge::modulation::params;

ModelStackWithAutoParam* AudioOutput::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                             int32_t paramID, params::Kind paramKind, bool affectEntire,
                                                             bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);

	// Require UNPATCHED_GLOBAL and a valid GLOBAL manager, preventing another parameter kind from resolving to an
	// unrelated global parameter.
	if (paramKind == params::Kind::UNPATCHED_GLOBAL && modelStackWithThreeMainThings
	    && modelStackWithThreeMainThings->paramManager
	    && modelStackWithThreeMainThings->paramManager->matches_type(required_param_manager_type())) {
		modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}
