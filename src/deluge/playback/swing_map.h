#pragma once
#include <cstdint>

/// How finer swing levels follow the primary swing knob. Stored per song and as a flash default.
enum class SwingFeel : uint8_t {
	CLASSIC = 0, ///< one level, as before swing feel existed
	NESTED = 1,  ///< each finer level gets half the previous level's amount (integer, toward zero)
	EVEN = 2,    ///< every level gets the full amount
};
constexpr uint8_t kNumSwingFeels = 3;
constexpr int32_t kMaxSwingLevels = 3;    // primary + up to two finer levels
constexpr int32_t kSwingMaxSyncLevel = 9; // == MAX_SYNC_LEVEL (model/sync.h)

struct SwingLevel {
	uint32_t halfTicks; // 3 << (9 - syncLevel); the level's block is 2 * halfTicks
	int32_t amount;     // -49..49, never 0 for an active level
};

struct SwingLevels {
	int32_t numLevels = 0;               // 0 => straight
	SwingLevel level[kMaxSwingLevels]{}; // level[0] = primary (coarsest)
	uint32_t blockTicks() const { return level[0].halfTicks * 2; }
};

/// amount -49..49, interval = swing sync level 1..9 (values above 9 behave like 9).
SwingLevels computeSwingLevels(int32_t amount, int32_t interval, SwingFeel feel);

/// Internal-tick position within the primary block of swung tick s (0 <= s <= blockTicks), as exact num/den.
struct SwingBlockPos {
	uint64_t num;
	uint64_t den; // 50^numLevels
};
SwingBlockPos swingForwardInBlock(const SwingLevels& levels, uint32_t swungTicksIntoBlock);

/// Inverse: floor of the swung tick at internal position num/den within the block (clamped to [0, blockTicks]).
uint32_t swingInverseInBlock(const SwingLevels& levels, uint64_t num, uint64_t den);

/// value * pos.num / pos.den without overflowing 64 bits (for 32.32 times: tiny rounding below 2^-16 of a sample).
uint64_t swingScaleByPos(uint64_t value, SwingBlockPos pos);
