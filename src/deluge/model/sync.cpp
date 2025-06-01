#include "model/sync.h"
#include "definitions_cxx.hpp"
#include "util/const_functions.h"

#ifdef IN_UNIT_TESTS
#include "display_mock.h"
#include "function_mocks.h"
#include "song_mock.h"
#else
#include "hid/display/display.h"
#include "model/song/song.h"
#include "util/functions.h"
#endif

int32_t wrapSwingIntervalSyncLevel(int32_t value) {
	// This is tricky because we don't want zero: that would be "off".
	return mod(value - 1, NUM_SWING_INTERVALS - 1) + 1;
}

int32_t clampSwingIntervalSyncLevel(int32_t value) {
	if (value < MIN_SWING_INERVAL) {
		return MIN_SWING_INERVAL;
	}
	else if (value > MAX_SWING_INTERVAL) {
		return MAX_SWING_INTERVAL;
	}
	else {
		return value;
	}
}

enum SyncType syncValueToSyncType(int32_t value) {
	if (value < SYNC_TYPE_TRIPLET) {
		return SYNC_TYPE_EVEN;
	}
	else if (value < SYNC_TYPE_DOTTED) {
		return SYNC_TYPE_TRIPLET;
	}
	else {
		return SYNC_TYPE_DOTTED;
	}
}

enum SyncLevel syncValueToSyncLevel(int32_t option) {
	if (option < SYNC_TYPE_TRIPLET) {
		return static_cast<enum SyncLevel>(option);
	}
	else if (option < SYNC_TYPE_DOTTED) {
		return static_cast<enum SyncLevel>(option - SYNC_TYPE_TRIPLET + 1);
	}
	else {
		return static_cast<enum SyncLevel>(option - SYNC_TYPE_DOTTED + 1);
	}
}

void syncValueToString(uint32_t value, StringBuf& buffer, int32_t tickMagnitude) {
	char const* typeStr = nullptr;
	enum SyncType type { syncValueToSyncType(value) };
	enum SyncLevel level { syncValueToSyncLevel(value) };

	uint32_t shift = SYNC_LEVEL_256TH - level;
	uint32_t noteLength = uint32_t{3} << shift;

	switch (type) {
	case SYNC_TYPE_EVEN:
		if (value != 0) {
			typeStr = "-notes";
		}
		break;
	case SYNC_TYPE_TRIPLET:
		typeStr = "-tplts";
		break;
	case SYNC_TYPE_DOTTED:
		typeStr = "-dtted";
		break;
	}

	getNoteLengthNameFromMagnitude(buffer, getNoteMagnitudeFfromNoteLength(noteLength, tickMagnitude), typeStr, false);
	if (typeStr != nullptr) {
		int32_t magnitudeLevelBars = SYNC_LEVEL_8TH - tickMagnitude;
		if (((type == SYNC_TYPE_TRIPLET || type == SYNC_TYPE_DOTTED) && level <= magnitudeLevelBars)
		    || display->have7SEG()) {
			// On OLED, getNoteLengthNameForMagniture() handles adding this for the non-bar levels. On 7seg, always
			// append it
			buffer.append(typeStr);
		}
	}
}

void syncValueToStringForHorzMenuLabel(SyncType type, SyncLevel level, StringBuf& buffer, int32_t tickMagnitude) {
	uint32_t shift = SYNC_LEVEL_256TH - level;
	uint32_t noteLength = uint32_t{3} << shift;

	getNoteLengthNameFromMagnitude(buffer, getNoteMagnitudeFfromNoteLength(noteLength, tickMagnitude), nullptr, false);

	// Remove all '-' characters
	DEF_STACK_STRING_BUF(tempBuf, 12);
	for (uint32_t i = 0; i < buffer.size(); ++i) {
		const char currentChar = buffer.c_str()[i];
		if (currentChar != '-' && currentChar != '\0') {
			tempBuf.append(currentChar);
		}
	}
	buffer.clear();
	buffer.append(tempBuf.c_str());
}
