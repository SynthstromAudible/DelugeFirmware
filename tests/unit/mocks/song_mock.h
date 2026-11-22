#pragma once

#include "util/d_string.h"

#include "clip_mocks.h"

class Song {
public:
	void deleteClipObject(Clip* clip, bool x, InstrumentRemoval removal) {
		(void)x;
		(void)removal;
		clip->deleted = true;
	}
	void clear() {
		sessionClips.clear();
		arrangementOnlyClips.clear();
	}
	void getNoteLengthName(StringBuf& buffer, uint32_t noteLength, char const* notesString = "-notes",
	                       bool clarifyPerColumn = false) const {
		// TODO: extract getNoteLengthName() from Song, so we can test
		// note length naming logic
	}
	ClipArray sessionClips;
	ClipArray arrangementOnlyClips;
	int32_t insideWorldTickMagnitude = 1;
};

extern Song* currentSong;
