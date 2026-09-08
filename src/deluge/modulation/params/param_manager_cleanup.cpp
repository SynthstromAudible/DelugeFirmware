#include "memory/general_memory_allocator.h"
#include "modulation/params/param_manager.h"

void ParamManager::forgetParamCollections() {
	summaries[0] = *getExpressionParamSetSummary();
	summaries[1] = {0};
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
	expressionParamSetOffset = 0;
}

void ParamManager::destructMainParamCollections() {
	if (expressionParamSetOffset != 0 && expressionParamSetOffset != 1 && expressionParamSetOffset != 3) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("PM0C");
#endif
		destructAndForgetParamCollections();
		return;
	}
	ParamCollectionSummary expression = summaries[expressionParamSetOffset];
	if (expression.paramCollection
	    && expression.paramCollection->getParamKind() == deluge::modulation::params::Kind::EXPRESSION) {
		for (auto& summary : summaries) {
			if (summary.paramCollection == expression.paramCollection) {
				summary = {0};
			}
		}
	}
	else {
		expression = {0};
	}
	destructAndForgetParamCollections();
	summaries[0] = expression;
}

void ParamManager::destructAndForgetParamCollections() {
	for (auto& summary : summaries) {
		auto* collection = summary.paramCollection;
		if (collection) {
			for (auto& alias : summaries) {
				if (alias.paramCollection == collection) {
					alias = {0};
				}
			}
			collection->~ParamCollection();
			delugeDealloc(collection);
		}
	}

	summaries[0] = {0};
	summaries[1] = {0};
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
	expressionParamSetOffset = 0;
}