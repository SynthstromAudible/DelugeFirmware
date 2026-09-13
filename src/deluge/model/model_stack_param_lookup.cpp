#include "model/model_stack.h"
#include "modulation/params/param_collection.h"

namespace params = deluge::modulation::params;

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getUnpatchedAutoParamFromId(int32_t newParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (paramManager
	    && (paramManager->matches_type(ParamManagerType::GLOBAL)
	        || paramManager->matches_type(ParamManagerType::SOUND))) {
		ParamCollectionSummary* summary = paramManager->getUnpatchedParamSetSummary();

		ModelStackWithParamId* modelStackWithParamId =
		    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

		modelStackWithParam = summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
	}
	return modelStackWithParam;
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getPatchedAutoParamFromId(int32_t newParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (paramManager && paramManager->matches_type(ParamManagerType::SOUND)) {
		ParamCollectionSummary* summary = paramManager->getPatchedParamSetSummary();

		ModelStackWithParamId* modelStackWithParamId =
		    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

		modelStackWithParam = summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
	}
	return modelStackWithParam;
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getPatchCableAutoParamFromId(int32_t newParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (paramManager && paramManager->matches_type(ParamManagerType::SOUND)) {
		ParamCollectionSummary* summary = paramManager->getPatchCableSetSummary();

		ModelStackWithParamId* modelStackWithParamId =
		    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

		modelStackWithParam = summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
	}
	return modelStackWithParam;
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getExpressionAutoParamFromID(int32_t newParamId) {
	// Reject malformed manager layouts and negative IDs; return a null autoParam when expression allocation fails.
	if (!paramManager || !paramManager->has_valid_layout() || newParamId < 0
	    || newParamId >= kNumExpressionDimensions) {
		return addParamCollectionAndId(nullptr, nullptr, 0)->addAutoParam(nullptr); // "No param"
	}

	if (!paramManager->ensureExpressionParamSetExists()) {
		return addParamCollectionAndId(nullptr, nullptr, 0)->addAutoParam(nullptr);
	}
	ParamCollectionSummary* summary = paramManager->getExpressionParamSetSummary();
	ModelStackWithParamId* modelStackWithParamId =
	    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

	return summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
}
