#pragma once

// TODO: Instead of this, make the real functions accessible (functions.h pulls in too much)
#include "etl/string.h"
#include "util/c_string.h"

#include <cstdint>

int32_t getNoteMagnitudeFfromNoteLength(uint32_t noteLength, int32_t tickMagnitude) {
	return 1;
}
void getNoteLengthNameFromMagnitude(etl::istring& buf, int32_t magnitude, char const* durrationSuffix = "-notes",
                                    bool clarifyPerColumn = false) {
	;
}
