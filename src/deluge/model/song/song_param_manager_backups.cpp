#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include <new>

// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerForExactClip(ModControllableAudio* modControllable, Clip* clip,
                                                        ParamManager* stealInto) {

	uint32_t keyWords[2];
	keyWords[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable));
	keyWords[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip));

	int32_t iCorrectClip = backedUpParamManagers.searchMultiWordExact(keyWords);

	if (iCorrectClip == -1) {
		return nullptr;
	}

	BackedUpParamManager* elementCorrectClip =
	    (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iCorrectClip);
	if (!elementCorrectClip->paramManager.matches_type(modControllable->required_param_manager_type())) {
		return nullptr;
	}

	if (stealInto) {
		stealInto->destructMainParamCollections();
		stealInto->stealParamCollectionsFrom(
		    &elementCorrectClip->paramManager,
		    true); // Steal expression params too - if they're here (slightly rare case).
		backedUpParamManagers.deleteAtIndex(iCorrectClip);
		return stealInto;
	}
	else {
		return &elementCorrectClip->paramManager;
	}
}

// If none for the correct Clip, return one for a different Clip - prioritizing NULL Clip.
// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
                                                              ParamManager* stealInto) {

	int32_t i_any_clip =
	    backedUpParamManagers.search(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable)),
	                                 GREATER_OR_EQUAL); // Search just by first word
	if (i_any_clip >= backedUpParamManagers.getNumElements()) {
		return nullptr;
	}
	BackedUpParamManager* element_any_clip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i_any_clip);
	if (element_any_clip->modControllable != modControllable) {
		return nullptr; // If nothing with even the correct modControllable at all, get out
	}

	int32_t i_correct_clip = -1;
	BackedUpParamManager* element_correct_clip = nullptr;
	const auto required_type = modControllable->required_param_manager_type();
	for (int32_t i = i_any_clip; i < backedUpParamManagers.getNumElements(); ++i) {
		auto* element = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);
		if (element->modControllable != modControllable) {
			break;
		}
		if (!element->paramManager.matches_type(required_type)) {
			continue;
		}
		// Entries are sorted by clip pointer, so a null-clip fallback comes first.
		if (!element_correct_clip || element->clip == clip) {
			i_correct_clip = i;
			element_correct_clip = element;
		}
		if (element->clip == clip) {
			break;
		}
	}
	if (!element_correct_clip) {
		return nullptr;
	}

	if (stealInto) {
		stealInto->destructMainParamCollections();
		// Steal expression params too - if they're here (slightly rare case).
		stealInto->stealParamCollectionsFrom(&element_correct_clip->paramManager, true);
		backedUpParamManagers.deleteAtIndex(i_correct_clip);
		return stealInto;
	}
	else {
		return &element_correct_clip->paramManager;
	}
}

// Steals stuff.
// shouldStealExpressionParamsToo should only be true to save expression params from being destructed (e.g. if the
// Clip is being destructed).
void Song::backUpParamManager(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
                              bool shouldStealExpressionParamsToo) {

	// An empty or expression-only manager can legitimately have been backed up already.
	if (paramManager->matches_type(ParamManagerType::NONE)) {
		return;
	}
#if ALPHA_OR_BETA_VERSION
	if (!paramManager->matches_type(modControllable->required_param_manager_type())) {
		FREEZE_WITH_ERROR("PM10");
	}
#endif
	// Never replace a usable backup with an incomplete manager in release builds either.
	if (!paramManager->matches_type(modControllable->required_param_manager_type())) {
		return;
	}

	uint32_t keyWords[2];
	keyWords[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable));
	keyWords[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip));

	int32_t indexToInsertAt;

	int32_t i = backedUpParamManagers.searchMultiWordExact(keyWords, &indexToInsertAt);

	BackedUpParamManager* element;

	// If one already existed...
	if (i != -1) {
		element = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		// Let's destroy it...
		element->paramManager.destructAndForgetParamCollections();

		// ...and replace it
doStealing:
		element->paramManager.stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
	}

	// Otherwise, insert one
	else {
		i = indexToInsertAt;
		Error error = backedUpParamManagers.insertAtIndex(i);

		// If RAM error...
		if (error != Error::NONE) {

			// Destroy paramManager
			paramManager->destructAndForgetParamCollections();
		}

		// Or if that went fine...
		else {
			element = new (backedUpParamManagers.getElementAddress(i)) BackedUpParamManager();

			element->modControllable = modControllable;
			element->clip = clip;
			goto doStealing;
		}
	}
}

void Song::deleteBackedUpParamManagersForClip(Clip* clip) {

	AudioEngine::logAction("Song::deleteBackedUpParamManagersForClip");

	// Ok, this is the one sticky one where we actually do have to go through every element
	int32_t i = 0;

	while (i < backedUpParamManagers.getNumElements()) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);
		if (backedUp->clip == clip) {

			AudioEngine::routineWithClusterLoading();

			// We ideally want to just set the Clip to NULL. We can just do this if the previous element didn't have
			// the same ModControllable
			if (i == 0
			    || ((BackedUpParamManager*)backedUpParamManagers.getElementAddress(i - 1))->modControllable
			           != backedUp->modControllable) {
				backedUp->clip = nullptr;
				i++;
			}

			// Othwerwise...
			else {

				ParamManagerForTimeline paramManager;
				paramManager.stealParamCollectionsFrom(&backedUp->paramManager);
				ModControllableAudio* modControllable = backedUp->modControllable;

				// We have to delete that element...
				// The main-only steal leaves expression behind; deleteAtIndex() does not run destructors.
				// Song backup regression tests caught that expression leaking when this entry was removed.
				backedUp->~BackedUpParamManager();
				backedUpParamManagers.deleteAtIndex(i);

				// ...and then go find the first one that had this ModControllable
				int32_t j = backedUpParamManagers.search(
				    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable)), GREATER_OR_EQUAL, 0,
				    i); // Search by first word only
				BackedUpParamManager* firstElementWithModControllable =
				    (BackedUpParamManager*)backedUpParamManagers.getElementAddress(j);

				// If it already had a NULL Clip, we have to replace its ParamManager
				if (!firstElementWithModControllable->clip) {
					firstElementWithModControllable->paramManager.destructAndForgetParamCollections();

					firstElementWithModControllable->paramManager.stealParamCollectionsFrom(&paramManager);

					// Don't increment i, as we've deleted an element instead
				}

				// Otherwise, we insert before it
				else {
					Error error = backedUpParamManagers.insertAtIndex(j);

					// If RAM error (surely would never happen since we just deleted an element)...
					if (error != Error::NONE) {
						// Don't increment i, as we've deleted an element instead
					}

					// Or if that went fine...
					else {
						BackedUpParamManager* newElement =
						    new (backedUpParamManagers.getElementAddress(j)) BackedUpParamManager();

						newElement->modControllable = modControllable;
						newElement->clip = nullptr;
						newElement->paramManager.stealParamCollectionsFrom(&paramManager);
						i++; // We deleted an element, but inserted one too
					}
				}
			}
		}
		else {
			i++;
		}
	}

	// Test that everything's still in order

#if ALPHA_OR_BETA_VERSION
	AudioEngine::routineWithClusterLoading();

	Clip* lastClip;
	ModControllableAudio* lastModControllable;

	for (int32_t i = 0; i < backedUpParamManagers.getNumElements(); i++) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		if (i >= 1) {

			if (backedUp->modControllable < lastModControllable) {
				FREEZE_WITH_ERROR("E053");
			}

			else if (backedUp->modControllable == lastModControllable) {
				if (backedUp->clip < lastClip) {
					FREEZE_WITH_ERROR("E054");
				}
				else if (backedUp->clip == lastClip) {
					FREEZE_WITH_ERROR("E055");
				}
			}
		}

		lastClip = backedUp->clip;
		lastModControllable = backedUp->modControllable;
	}

#endif
}

void Song::deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable) {

	int32_t iAnyClip = backedUpParamManagers.search(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable)),
	                                                GREATER_OR_EQUAL); // Search by first word only

	while (true) {
		if (iAnyClip >= backedUpParamManagers.getNumElements()) {
			return;
		}
		BackedUpParamManager* elementAnyClip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iAnyClip);
		if (elementAnyClip->modControllable != modControllable) {
			return;
		}

		// Destruct paramManager
		elementAnyClip->~BackedUpParamManager();

		// Delete from Vector
		backedUpParamManagers.deleteAtIndex(iAnyClip);
	}
}
