#include "model/scale/scale_mapper.h"
#include "definitions.h"

bool oops(const char* msg) {
#ifdef IN_UNIT_TESTS
	std::cerr << "conputeChangeFrom failed with " << msg << std::endl;
#else
	FREEZE_WITH_ERROR(msg);
#endif
	return false;
}

bool ScaleMapper::computeChangeFrom(NoteSet notes, NoteSet sourceScale, NoteSet targetScale, ScaleChange& changes) {
	if (notes.scaleSize() > targetScale.scaleSize() || !notes.isSubsetOf(sourceScale)) {
		return oops("SM01");
	}
	changes.source = sourceScale;
	// If we've previously converted from a scale with different size, this is the scale in which we
	// arrived at current scale size before converting to whatever scale we're in now. If we don't have
	// a different scale size in our history, this is same as source scale.
	NoteSet initialScale = initialTransitionScale(sourceScale);
	// Compute changes[] needed to go from into transition scale.
	computeInitialChanges(sourceScale, initialScale, changes);
	// Tranform notes into the transition scale.
	NoteSet transitionNotes = changes.applyTo(notes);
	// If we've added new notes not part of a previous transition, we need to flush out
	// the earlier transitions: they're no longer valid.
	if (!transitionNotes.isSubsetOf(lastTransitionNotes)) {
		flushTransitionScaleStore(initialScale);
	}

	NoteSet transitionScale = initialScale;
	// Step the transition scale until it is the same size as target scale.
	uint8_t size = transitionScale.count();
	while (size != targetScale.count()) {
		// Each step should only add or remove one note from the scale: we don't need
		// to think about changes[] because the notes aren't _moving_, which is alo why
		// we don't need to update transitionNotes.
		transitionScale = nextTransitionScale(transitionNotes, transitionScale, targetScale);
		uint8_t newSize = transitionScale.count();
		if (newSize == size) {
			// Bail out if we don't make progesss.
			return oops("SM02");
		}
		else {
			size = newSize;
		}
	}
	// Store the final transition notes, so we know if we need to flush the transition scale store.
	lastTransitionNotes = transitionNotes;

	// Now compute the changes needed to go from transitionScale of correct size to the target
	// scale.
	computeFinalChanges(initialScale, transitionScale, targetScale, changes);
	return true;
}

NoteSet ScaleMapper::initialTransitionScale(NoteSet sourceScale) {
	uint8_t size = sourceScale.count();
	NoteSet mode = transitionScaleStore[size];
	if (mode.isEmpty()) {
		mode = transitionScaleStore[size] = sourceScale;
	}
	return mode;
}

void ScaleMapper::computeInitialChanges(NoteSet sourceScale, NoteSet initialScale, ScaleChange& changes) {
	// Zero the root offset
	changes[0] = 0;
	uint8_t size = sourceScale.count();
	// Set the semitone offsets to go from source to the initial transition scale
	for (uint8_t degree = 1; degree < size; degree++) {
		changes[degree] = initialScale[degree] - sourceScale[degree];
	}
	// Zero out the tail end.
	for (uint8_t degree = size; degree < NoteSet::size; degree++) {
		changes[degree] = 0;
	}
}

void ScaleMapper::flushTransitionScaleStore(NoteSet initialScale) {
	for (int i = 0; i < kMaxScaleSize; i++) {
		transitionScaleStore[i].clear();
	}
	transitionScaleStore[initialScale.count()] = initialScale;
}

void ScaleMapper::computeFinalChanges(NoteSet initialScale, NoteSet transitionScale, NoteSet targetScale,
                                      ScaleChange& changes) {
	// Initial scale has the same number of scale degrees as actual source scale.
	//
	// Transition scale may have more or less scale degrees, but if the same _note_ is present in
	// both init and transition, it refers to the same source scale degree.
	//
	// Target scale has the same number of scale degrees as transition.
	//
	// Bit of debugging prints left out in commented form. This function is a key place to instrument
	// if you want to understand what's going on.
	/*
	    std::cerr << "source=" << source << std::endl;
	    std::cerr << "transition=" << transition << std::endl;
	    std::cerr << "target=" << target << std::endl;
	*/
	for (uint8_t sourceDegree = 1; sourceDegree < initialScale.count(); sourceDegree++) {
		uint8_t sourceNote = initialScale[sourceDegree];
		if (transitionScale.has(sourceNote)) {
			uint8_t transitionDegree = transitionScale.degreeOf(sourceNote);
			uint8_t targetNote = targetScale[transitionDegree];
			// NoteSet x, y; x.add(sourceNote); y.add(targetNote);
			// std::cerr << "mapping degree " << (int)sourceDegree << " to " << (int)transitionDegree << " as " << x <<
			// " -> " << y << std::endl;
			changes[sourceDegree] += targetNote - sourceNote;
		}
	}
}

NoteSet ScaleMapper::nextTransitionScale(NoteSet notes, NoteSet transitionScale, NoteSet targetScale) {
	uint8_t ss = transitionScale.count();
	uint8_t ts = targetScale.count();
	uint8_t next = ss;
	if (ss > ts) {
		next = ss - 1;
	}
	else if (ts > ss) {
		next = ss + 1;
	}
	NoteSet nextScale = transitionScaleStore[next];
	if (nextScale.isEmpty()) {
		// No transition mode for this size, let's make one up.
		nextScale = transitionScale;
		if (ss > ts) {
			// Drop an unused note.
			//
			// *** We can drop any note that isn't in notes. ***
			//
			// It is better to drop high notes than low notes: the lower
			// you drop the more likely you are to change the function of
			// other notes. (You're still going to change the function regularly,
			// but _sometimes_ this will avoid it.)
			//
			// Another trick is to try to pick notes that aren't used in
			// the target scale either: this way we're more likely to preserve
			// intervals. (We're still going to change intervals, but _sometimes_
			// this will avoid it.)
			int8_t inNeither = nextScale.highestNotIn(notes | targetScale);
			if (inNeither < 0) {
				nextScale.remove(nextScale.highestNotIn(notes));
			}
			else {
				nextScale.remove(inNeither);
			}
		}
		else if (ss < ts) {
			// Add highest note of target scale we don't already have.
			nextScale.add(targetScale.highestNotIn(transitionScale));
		}
		transitionScaleStore[next] = nextScale;
	}
	return nextScale;
}
