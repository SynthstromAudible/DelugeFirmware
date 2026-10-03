#pragma once

#include "etl/string.h"
#include "util/c_string.h"

#include "clip_mocks.h"

class Song {
public:
	void deleteClipObject(Clip* clip, bool x, InstrumentRemoval removal) {
		(void)x;
		(void)removal;
		clip->deleted = true;
	}
	void clear() {
		live.clips.session_clips.clear();
		live.clips.arrangement_only_clips.clear();
	}
	void getNoteLengthName(etl::istring& buffer, uint32_t noteLength, char const* notesString = "-notes",
	                       bool clarifyPerColumn = false) const {
		// TODO: extract getNoteLengthName() from Song, so we can test
		// note length naming logic
	}
	// Mirror the real Song's shape: the clip arrays live under live.clips
	// (deluge::bridge::Clips { session_clips, arrangement_only_clips }).
	struct Live {
		struct Clips {
			ClipArray session_clips;
			ClipArray arrangement_only_clips;
		} clips;
	} live;
	int32_t insideWorldTickMagnitude = 1;
};

extern Song* currentSong;
