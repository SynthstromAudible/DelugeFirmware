#include "model/sync.h"
#include "definitions_cxx.hpp"
#include "util/const_functions.h"

#ifdef IN_UNIT_TESTS
#include "display_mock.h"
#include "song_mock.h"
#else
#include "hid/display/display.h"
#include "model/song/song.h"
#endif

int32_t wrapSwingIntervalSyncLevel(int32_t value) {
	// This is tricky because we don't want zero: that would be "off".
	return mod(value - 1, NUM_SWING_INTERVALS - 1) + 1;
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

void syncValueToString(uint32_t value, StringBuf& buffer) {
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

	currentSong->getNoteLengthName(buffer, noteLength, typeStr);
	if (typeStr != nullptr) {
		int32_t magnitudeLevelBars = SYNC_LEVEL_8TH - currentSong->insideWorldTickMagnitude;
		if (((type == SYNC_TYPE_TRIPLET || type == SYNC_TYPE_DOTTED) && level <= magnitudeLevelBars)
		    || display->have7SEG()) {
			// On OLED, getNoteLengthName handles adding this for the non-bar levels. On 7seg, always append it
			buffer.append(typeStr);
		}
	}
}
