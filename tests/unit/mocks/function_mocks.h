#pragma once

// TODO: Instead of this, make the real functions accessible (functions.h pulls in too much)
#include "util/d_string.h"

#include <cstdint>

int32_t getNoteMagnitudeFfromNoteLength(uint32_t noteLength, int32_t tickMagnitude) {
	return 1;
}
void getNoteLengthNameFromMagnitude(StackString& buf, int32_t magnitude, char const* durrationSuffix = "-notes",
                                    bool clarifyPerColumn = false) {
	;
}
