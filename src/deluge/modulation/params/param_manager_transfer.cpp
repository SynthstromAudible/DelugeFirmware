#include "memory/general_memory_allocator.h"
#include "modulation/params/param_manager.h"
#include <cstring>

// Make sure other isn't NULL before you call this, you muppet.
void ParamManager::stealParamCollectionsFrom(ParamManager* other, bool stealExpressionParams) {
#if ALPHA_OR_BETA_VERSION
	if (!other) {
		FREEZE_WITH_ERROR("PM0B"); // was E413
	}
#endif

#if ALPHA_OR_BETA_VERSION
	if (other == this || !other->has_valid_layout() || !has_valid_layout()) {
		FREEZE_WITH_ERROR("PM0E");
	}
#endif

	// Populated-destination regressions caught overwritten main collections leaking here.
	destructMainParamCollections();
	int32_t mpeParamsOffsetOther = other->getExpressionParamSetOffset();
	int32_t mpeParamsOffsetHere = getExpressionParamSetOffset();
	int32_t stopAtOther = mpeParamsOffsetOther;

	// If we're planning to steal expression params, and yes "other" does in fact have them...
	if (stealExpressionParams && other->summaries[stopAtOther].paramCollection) {

		// If "here" has them too, we'll just keep these, and destruct "other"'s ones
		if (summaries[mpeParamsOffsetHere].paramCollection) {
			other->summaries[stopAtOther].paramCollection->~ParamCollection();
			delugeDealloc(other->summaries[stopAtOther].paramCollection);
			other->summaries[stopAtOther] = {0};
		}

		// Otherwise, yup, proceed to steal them
		else {
			stopAtOther++;
		}
	}

	ParamCollectionSummary hereMpeParamsOrNull = summaries[mpeParamsOffsetHere];

	int32_t i;
	for (i = 0; i < stopAtOther; i++) {
		summaries[i] = other->summaries[i];
	}

	summaries[stopAtOther] = hereMpeParamsOrNull; // Could the expression params, or NULL
	// Anything past the terminator would be left over from our previous, longer layout, and would make the layout
	// predicates reject us.
	for (int32_t j = stopAtOther + (hereMpeParamsOrNull.paramCollection ? 1 : 0); j < PARAM_COLLECTIONS_STORAGE_NUM;
	     j++) {
		summaries[j] = {0};
	}
	expressionParamSetOffset = mpeParamsOffsetOther;

	other->summaries[0] = other->summaries[stopAtOther];
	other->summaries[1] = {0};
	other->summaries[2] = {0};
	other->summaries[3] = {0};
	other->summaries[4] = {0};
	other->expressionParamSetOffset = 0;
}

Error ParamManager::cloneParamCollectionsFrom(ParamManager const* other, bool copyAutomation,
                                              bool cloneExpressionParams, int32_t reverseDirectionWithLength) {

#if ALPHA_OR_BETA_VERSION
	if (!other || !other->has_valid_layout() || !has_valid_layout()) {
		FREEZE_WITH_ERROR("PM0F");
	}
#endif

	ParamCollectionSummary mpeParamsOrNullHere = *getExpressionParamSetSummary();
	// Paul: Prevent MPE data from not getting exchanged with a newly allocated pointer if we allocate the same params
	// for another clip
	if (this != other) {
		if (mpeParamsOrNullHere.paramCollection) {
			cloneExpressionParams = false; // If we already have expression params, then just don't clone from "other".
		}
	}

	// First, allocate the memories
	ParamCollectionSummary
	    newSummaries[PARAM_COLLECTIONS_STORAGE_NUM]; // Temporary separate storage, so we can clone from self (when this
	                                                 // function is called from beenCloned()).

	ParamCollectionSummary* __restrict__ newSummary = newSummaries;
	ParamCollectionSummary const* otherSummary =
	    other->summaries; // Not __restrict__, because other might be the same as this!
	ParamCollectionSummary const* otherStopAt = &other->summaries[other->expressionParamSetOffset];

	if (cloneExpressionParams && otherStopAt->paramCollection) {
		otherStopAt++;
	}

	while (otherSummary != otherStopAt) {
		// To cut corners, we store this currently blank/undefined memory in our array of type ParamCollectionSummary
		newSummary->paramCollection =
		    (ParamCollection*)GeneralMemoryAllocator::get().allocMaxSpeed(otherSummary->paramCollection->objectSize);

		// If that failed, deallocate all the previous memories
		if (!newSummary->paramCollection) {
			while (newSummary != newSummaries) {
				newSummary--;
				delugeDealloc(newSummary->paramCollection);
			}

			if (this == other) {
				// beenCloned() operates on a shallow copy of a NoteRow. None of these pointers,
				// including expression, belong to the new row until cloning succeeds.
				for (auto& summary : summaries) {
					summary = {0};
				}
				expressionParamSetOffset = 0;
			}
			// For a distinct source, leave our existing collections and expression untouched.
			return Error::INSUFFICIENT_RAM;
		}

		newSummary++;
		otherSummary++;
	}

	// Now the memories have been allocated, go through and do the cloning
	newSummary = newSummaries;
	otherSummary = other->summaries;
	while (otherSummary != otherStopAt) {

		memcpy(newSummary->paramCollection, otherSummary->paramCollection, otherSummary->paramCollection->objectSize);

		if (copyAutomation) {
			newSummary->cloneFlagsFrom(otherSummary);
		}
		else {
			// The cloned collections contain scalars only. Flags copied from the source
			// would schedule automation/interpolation for parameters with no nodes.
			newSummary->resetAutomationRecord(kMaxNumUnsignedIntegerstoRepAllParams - 1);
			newSummary->resetInterpolationRecord(kMaxNumUnsignedIntegerstoRepAllParams - 1);
		}

		// Initialize flags first so collection-specific cloning can clear entries
		// whose automation could not be copied (e.g. a node allocation failure).
		newSummary->paramCollection->beenCloned(copyAutomation, reverseDirectionWithLength, newSummary);

		newSummary++;
		otherSummary++;
	}

	// Paul: If we move allocation position of the same clip mpe data was allocated above and doesn't require special
	// treatment
	if (this == other) {
		*newSummary = {0}; // Mark end of list
	}
	else {
		*newSummary = mpeParamsOrNullHere;
		if (mpeParamsOrNullHere.paramCollection) { // Check first, otherwise we'll overflow the array, I think...
			newSummary++;
			*newSummary = {0}; // Mark end of list
		}
	}

	// Commit only after allocation succeeds; self-cloning owns none of the original pointers.
	if (this != other) {
		destructMainParamCollections();
	}
	// And finally, copy the pointers and flags from newSummaries to our permanent summaries array.
	newSummary = newSummaries;
	ParamCollectionSummary* destSummaries = summaries;
	while (true) {
		*destSummaries = *newSummary;
		if (!newSummary->paramCollection) {
			break;
		}
		destSummaries++;
		newSummary++;
	}
	// Wipe anything left over from a previous, longer layout, so the layout predicates don't see a stale pointer
	// past the terminator.
	for (ParamCollectionSummary* stale = destSummaries + 1; stale != &summaries[PARAM_COLLECTIONS_STORAGE_NUM];
	     stale++) {
		*stale = {0};
	}

	expressionParamSetOffset = other->expressionParamSetOffset;

	return Error::NONE;
}

// This is only called once - for NoteRows after cloning an InstrumentClip.
Error ParamManager::beenCloned(int32_t reverseDirectionWithLength) {
	return cloneParamCollectionsFrom(this, true, true, reverseDirectionWithLength); // *Does* clone expression params
}