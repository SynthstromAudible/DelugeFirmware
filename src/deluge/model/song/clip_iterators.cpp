#include "model/song/clip_iterators.h"
#include <iterator>

ClipIteratorBase::ClipIteratorBase(ClipArray* array_, uint32_t index_, ClipArray* nextArray_,
                                   std::optional<ClipType> clipType_)
    : array(array_), nextArray(nextArray_), index(index_), clipType(clipType_) {
	// handle case of first array empty on initialization, or first element being
	// wrong type
	index--;
	next();
}

ClipIteratorBase& ClipIteratorBase::operator++() {
	next();
	return *this;
}
ClipIteratorBase ClipIteratorBase::operator++(int) {
	ClipIteratorBase pre = *this;
	next();
	return pre;
}

void ClipIteratorBase::deleteClip(InstrumentRemoval instrumentRemoval) {
	Clip* clip = (*array)[index];
	currentSong->deleteClipObject(clip, false, instrumentRemoval);
	// This shifts all others down by one.
	array->erase(array->begin() + index);
	// By decreementing the index and doing next we force the type filter
	// to run again. We don't actually need to loop backwards -- only
	// forwards.
	index--;
	next();
}

void ClipIteratorBase::next() {
	for (;;) {
		index++;
		if (index >= std::ssize(*array) && nextArray) {
			array = nextArray;
			nextArray = nullptr;
			index = 0;
		}
		if (index >= std::ssize(*array)) {
			// end
			return;
		}
		if (!clipType.has_value() || clipType.value() == (*array)[index]->type) {
			// valid clip for this iteration
			return;
		}
	}
}

AllClips AllClips::everywhere(Song* song) {
	return AllClips(&song->sessionClips, &song->arrangementOnlyClips);
}

AllClips AllClips::inSession(Song* song) {
	return AllClips(&song->sessionClips, nullptr);
}

AllClips AllClips::inArrangementOnly(Song* song) {
	return AllClips(&song->arrangementOnlyClips, nullptr);
}

ClipIterator<Clip> AllClips::begin() {
	return ClipIterator<Clip>(first, 0, second, {});
}

ClipIterator<Clip> AllClips::end() {
	ClipArray* last = second ? second : first;
	int32_t n = last ? static_cast<int32_t>(last->size()) : 0;
	return ClipIterator<Clip>(last, n, nullptr, {});
}

InstrumentClips InstrumentClips::everywhere(Song* song) {
	return InstrumentClips(&song->sessionClips, &song->arrangementOnlyClips);
}

ClipIterator<InstrumentClip> InstrumentClips::begin() {
	return ClipIterator<InstrumentClip>(first, 0, second, ClipType::INSTRUMENT);
}

ClipIterator<InstrumentClip> InstrumentClips::end() {
	ClipArray* last = second ? second : first;
	return ClipIterator<InstrumentClip>(last, static_cast<int32_t>(last->size()), nullptr, ClipType::INSTRUMENT);
}

AudioClips AudioClips::everywhere(Song* song) {
	return AudioClips(&song->sessionClips, &song->arrangementOnlyClips);
}

ClipIterator<AudioClip> AudioClips::begin() {
	return ClipIterator<AudioClip>(first, 0, second, ClipType::AUDIO);
}

ClipIterator<AudioClip> AudioClips::end() {
	ClipArray* last = second ? second : first;
	return ClipIterator<AudioClip>(last, static_cast<int32_t>(last->size()), nullptr, ClipType::AUDIO);
}
