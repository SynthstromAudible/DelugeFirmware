#include "memory/general_memory_allocator.h"
#include "modulation/params/param_manager.h"

// Does *not* forget MPE params
void ParamManager::forgetParamCollections() {
	summaries[0] = *getExpressionParamSetSummary();
	summaries[1] = {0};
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
	expressionParamSetOffset = 0;
}

// Main collections belong to the sound/output; expression belongs to the clip or note row. Also disposes of a
// non-expression collection sitting in the expression slot, which an incompatible manager can have.
void ParamManager::destructMainParamCollections() {
	if (expressionParamSetOffset != 0 && expressionParamSetOffset != 1 && expressionParamSetOffset != 3) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("PM0C");
#endif
		// The expression slot cannot be trusted. Do not pass this offset to either accessor below.
		destructAndForgetParamCollections();
		return;
	}
	// An incompatible manager may also have the wrong collection in the expression slot.
	ParamCollectionSummary expression = summaries[expressionParamSetOffset];
	if (expression.paramCollection
	    && expression.paramCollection->getParamKind() == deluge::modulation::params::Kind::EXPRESSION) {
		// Detach every alias before cleanup so a malformed layout cannot free the expression we retain.
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

// This one deletes MPE params too
void ParamManager::destructAndForgetParamCollections() {
	// Cleanup regressions exposed leaks after null slots. Scan all storage, including malformed layout tails.
	for (auto& summary : summaries) {
		auto* collection = summary.paramCollection;
		if (collection) {
			// Clear duplicate entries before freeing to avoid double destruction of an aliased collection.
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
