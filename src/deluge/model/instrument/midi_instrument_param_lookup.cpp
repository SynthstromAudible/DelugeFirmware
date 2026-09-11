#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"

ModelStackWithAutoParam* MIDIInstrument::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                                int32_t paramID,
                                                                deluge::modulation::params::Kind paramKind,
                                                                bool affectEntire, bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);

	if (modelStackWithThreeMainThings) {
		ParamManager* paramManager = modelStackWithThreeMainThings->paramManager;

		if (paramManager && paramManager->matches_type(required_param_manager_type())) {

			modelStackWithParam = getParamToControlFromInputMIDIChannel(paramID, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MIDIInstrument::getParamToControlFromInputMIDIChannel(int32_t cc, ModelStackWithThreeMainThings* modelStack) {
	// ensure that we are trying to create a param for a valid cc number
	bool is_cc_valid = ((cc >= 0) && (cc < kNumCCExpression));

	// if cc is not valid or param manager is null (which can happen if the user is holding down an audition pad in
	// Arranger, and we have no clips)
	if (!is_cc_valid || !modelStack->paramManager) {
noParam:
		return modelStack->addParamCollectionAndId(nullptr, nullptr, 0)->addAutoParam(nullptr); // "No param"
	}

	ParamCollectionSummary* summary;
	int32_t paramId = cc;

	switch (cc) {
	case CC_NUMBER_PITCH_BEND:
		paramId = 0;
		goto expressionParam;
	case CC_NUMBER_Y_AXIS:
		paramId = 1;
		goto expressionParam;

	case CC_NUMBER_AFTERTOUCH:
		paramId = 2;
expressionParam:
		modelStack->paramManager->ensureExpressionParamSetExists(); // Allowed to fail
		summary = modelStack->paramManager->getExpressionParamSetSummary();
		if (!summary->paramCollection) {
			goto noParam;
		}
		break;

	default:
		summary = modelStack->paramManager->getMIDIParamCollectionSummary();
		break;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	return summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId,
	    true); // Yes we do want to force creating it even if we're not recording - so the level indicator can update
	           // for the user
}
