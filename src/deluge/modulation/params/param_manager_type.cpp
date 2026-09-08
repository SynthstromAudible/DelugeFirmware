#include "modulation/params/param_manager.h"

#if ALPHA_OR_BETA_VERSION
ParamManagerForTimeline* ParamManager::toForTimeline() {
	FREEZE_WITH_ERROR("PM09");
	return nullptr;
}

ParamManagerForTimeline* ParamManagerForTimeline::toForTimeline() {
	return this;
}
#endif

ParamManagerForTimeline::ParamManagerForTimeline() {
	ticksSkipped = 0;
	ticksTilNextEvent = 0;
}