#pragma once

#include <cstdint>

#include "util/d_string.h"

// SyncType values correspond to the index of the first option of the specific
// type in the selection menu. There are 9 different levels for each type (see
// also SyncLevel)
enum SyncType : uint8_t {
	SYNC_TYPE_EVEN = 0,
	SYNC_TYPE_TRIPLET = 10,
	SYNC_TYPE_DOTTED = 19,
};

// Triplet swing not supported yet.
#define MIN_SWING_INERVAL 1
#define MAX_SWING_INTERVAL (SYNC_TYPE_TRIPLET - 1)
#define NUM_SWING_INTERVALS SYNC_TYPE_TRIPLET

// NOTE: These names are correct only for default resolution!
enum SyncLevel : uint8_t {
	SYNC_LEVEL_NONE = 0,
	SYNC_LEVEL_WHOLE = 1,
	SYNC_LEVEL_2ND = 2,
	SYNC_LEVEL_4TH = 3,
	SYNC_LEVEL_8TH = 4,
	SYNC_LEVEL_16TH = 5,
	SYNC_LEVEL_32ND = 6,
	SYNC_LEVEL_64TH = 7,
	SYNC_LEVEL_128TH = 8,
	SYNC_LEVEL_256TH = 9,
};

#define MAX_SYNC_LEVEL 9
#define NUM_SYNC_VALUES 28

enum SyncLevel syncValueToSyncLevel(int32_t option);

enum SyncType syncValueToSyncType(int32_t value);

void syncValueToString(uint32_t value, StringBuf& buffer, int32_t tickMagnitude);
void syncValueToStringForHorzMenuLabel(SyncType type, SyncLevel level, StringBuf& buffer, int32_t tickMagnitude);

/** Modulus of value as a non-zero SyncLevel valid for Swing interval. */
int32_t wrapSwingIntervalSyncLevel(int32_t value);

/** Clamp value to a valid Swing interval. */
int32_t clampSwingIntervalSyncLevel(int32_t value);
