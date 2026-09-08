#include "model/model_stack.h"
#include "modulation/params/param_manager.h"
#include <algorithm>

// You'll usually want to call mightContainAutomation() before bothering with this, to save time.
void ParamManagerForTimeline::processCurrentPos(ModelStackWithThreeMainThings* modelStack, int32_t ticksSinceLast,
                                                bool reversed, bool didPingpong, bool mayInterpolate) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections();
#endif
	ticksSkipped += ticksSinceLast;
	ticksTilNextEvent -= ticksSinceLast;
	if (ticksTilNextEvent <= 0) {
		ticksTilNextEvent = 2147483647;
		ParamCollectionSummary* summary = summaries;
		do {
			if (summary->containsAutomation()) {
				auto* collectionStack = modelStack->addParamCollectionSummary(summary);
				// If we can't interpolate by samples then we'll interpolate by ticks instead. This has to happen
				// *before* processCurrentPos(), because that may reach a node and set up a new increment for the span
				// we're about to begin - whereas the ticks we've skipped belong to the span we've just finished.
				// Applying them to the new increment sends the value far past its target (a whole inter-node gap's
				// worth of a few-tick ramp), which for MIDI output means expression values well outside 0-127.
				if (!mayInterpolate && summary->whichParamsAreInterpolating[0] != 0u) {
					summary->paramCollection->tickTicks(ticksSkipped, collectionStack);
				}
				summary->paramCollection->processCurrentPos(collectionStack, ticksSkipped, reversed, didPingpong, true);
				// Re-check after processCurrentPos(): if a node has just started some interpolation, we need to come
				// back every tick to advance it.
				if (!mayInterpolate && summary->whichParamsAreInterpolating[0] != 0u) {
					ticksTilNextEvent = 0;
				}
				ticksTilNextEvent = std::min(ticksTilNextEvent, summary->paramCollection->ticksTilNextEvent);
			}
			++summary;
		} while (summary->paramCollection);
		ticksSkipped = 0;
	}
}
