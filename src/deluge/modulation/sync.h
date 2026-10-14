#pragma once

#include "util/etl_string.h"
#include <cstdint>

#include "util/c_string.h"

#include "engine-api/types/sync.h"

using deluge::engine::SyncLevel;
using deluge::engine::SyncType;

inline constexpr SyncType SYNC_TYPE_EVEN = SyncType::EVEN;
inline constexpr SyncType SYNC_TYPE_TRIPLET = SyncType::TRIPLET;
inline constexpr SyncType SYNC_TYPE_DOTTED = SyncType::DOTTED;

// Triplet swing not supported yet.
#define MIN_SWING_INERVAL 1
#define MAX_SWING_INTERVAL (std::to_underlying(SyncType::TRIPLET) - 1)
#define NUM_SWING_INTERVALS std::to_underlying(SyncType::TRIPLET)

inline constexpr SyncLevel SYNC_LEVEL_NONE = SyncLevel::NONE;
inline constexpr SyncLevel SYNC_LEVEL_WHOLE = SyncLevel::WHOLE;
inline constexpr SyncLevel SYNC_LEVEL_2ND = SyncLevel::HALF;
inline constexpr SyncLevel SYNC_LEVEL_4TH = SyncLevel::QUARTER;
inline constexpr SyncLevel SYNC_LEVEL_8TH = SyncLevel::EIGHTH;
inline constexpr SyncLevel SYNC_LEVEL_16TH = SyncLevel::SIXTEENTH;
inline constexpr SyncLevel SYNC_LEVEL_32ND = SyncLevel::THIRTY_SECOND;
inline constexpr SyncLevel SYNC_LEVEL_64TH = SyncLevel::SIXTY_FOURTH;
inline constexpr SyncLevel SYNC_LEVEL_128TH = SyncLevel::ONE_HUNDRED_TWENTY_EIGHTH;
inline constexpr SyncLevel SYNC_LEVEL_256TH = SyncLevel::TWO_HUNDRED_FIFTY_SIXTH;

#define MAX_SYNC_LEVEL 9
#define NUM_SYNC_VALUES 28

SyncLevel syncValueToSyncLevel(int32_t option);

SyncType syncValueToSyncType(int32_t value);

void syncValueToString(uint32_t value, etl::istring& buffer, int32_t tickMagnitude);
void syncValueToStringForHorzMenuLabel(SyncType type, SyncLevel level, etl::istring& buffer, int32_t tickMagnitude);

/** Modulus of value as a non-zero SyncLevel valid for Swing interval. */
int32_t wrapSwingIntervalSyncLevel(int32_t value);

/** Clamp value to a valid Swing interval. */
int32_t clampSwingIntervalSyncLevel(int32_t value);
