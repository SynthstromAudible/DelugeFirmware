#include "model/clip/clip.h"
#include "model/instrument/melodic_instrument.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/model_stack.h"

namespace params = deluge::modulation::params;

ModelStackWithAutoParam* MelodicInstrument::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack,
                                                                   Clip* clip, int32_t paramID,
                                                                   deluge::modulation::params::Kind paramKind,
                                                                   bool affectEntire, bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);

	// Require SOUND for sound parameters, while permitting expression lookup on other valid layouts, including CV.
	if (modelStackWithThreeMainThings && modelStackWithThreeMainThings->paramManager
	    && (modelStackWithThreeMainThings->paramManager->matches_type(
	            toModControllable()->required_param_manager_type())
	        || (paramKind == params::Kind::EXPRESSION
	            && modelStackWithThreeMainThings->paramManager->has_valid_layout()))) {
		if (paramKind == deluge::modulation::params::Kind::PATCHED) {
			modelStackWithParam = modelStackWithThreeMainThings->getPatchedAutoParamFromId(paramID);
		}

		else if (paramKind == deluge::modulation::params::Kind::UNPATCHED_SOUND) {
			modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
		}

		else if (paramKind == deluge::modulation::params::Kind::PATCH_CABLE) {
			modelStackWithParam = modelStackWithThreeMainThings->getPatchCableAutoParamFromId(paramID);
		}
		else if (paramKind == deluge::modulation::params::Kind::EXPRESSION) {

			modelStackWithParam = modelStackWithThreeMainThings->getExpressionAutoParamFromID(paramID);
		}
	}

	return modelStackWithParam;
}
