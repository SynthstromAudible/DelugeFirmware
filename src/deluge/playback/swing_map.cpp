#include "playback/swing_map.h"
#include <algorithm>

SwingLevels computeSwingLevels(int32_t amount, int32_t interval, SwingFeel feel) {
	SwingLevels levels;
	if (amount == 0) {
		return levels;
	}
	int32_t leftShift = std::max<int32_t>(kSwingMaxSyncLevel - interval, 0);
	uint32_t halfTicks = 3u << leftShift;
	levels.level[0] = {halfTicks, amount};
	levels.numLevels = 1;

	if (feel == SwingFeel::CLASSIC) {
		return levels;
	}
	int32_t levelAmount = amount;
	for (int32_t j = 1; j < kMaxSwingLevels && interval + j <= kSwingMaxSyncLevel; j++) {
		if (feel == SwingFeel::NESTED) {
			levelAmount /= 2; // Truncates toward zero
		}
		if (levelAmount == 0) {
			break;
		}
		levels.level[j] = {halfTicks >> j, levelAmount};
		levels.numLevels++;
	}
	return levels;
}

// Each level's block is split at its midpoint: the first half is stretched by (50 + amount) / 50 from the block
// start, the second half by (50 - amount) / 50 measured back from the block end. Block edges stay fixed.

SwingBlockPos swingForwardInBlock(const SwingLevels& levels, uint32_t swungTicksIntoBlock) {
	uint64_t num = swungTicksIntoBlock;
	uint64_t den = 1;
	// Finest level first, so each coarser level sees the already warped position
	for (int32_t j = levels.numLevels - 1; j >= 0; j--) {
		const SwingLevel& level = levels.level[j];
		uint64_t blockNum = 2 * level.halfTicks * den;
		uint64_t blockStart = num / blockNum * blockNum;
		uint64_t r = num - blockStart;
		uint64_t warped = (r <= level.halfTicks * den) ? r * (50 + level.amount)
		                                               : blockNum * 50 - (blockNum - r) * (50 - level.amount);
		num = blockStart * 50 + warped;
		den *= 50;
	}
	return {num, den};
}

uint32_t swingInverseInBlock(const SwingLevels& levels, uint64_t num, uint64_t den) {
	uint32_t blockTicks = levels.blockTicks();
	if (num >= blockTicks * den) {
		return blockTicks;
	}
	// Coarsest level first, undoing the forward warps in reverse order
	for (int32_t j = 0; j < levels.numLevels; j++) {
		const SwingLevel& level = levels.level[j];
		uint64_t blockNum = 2 * level.halfTicks * den;
		uint64_t blockStart = num / blockNum * blockNum;
		uint64_t r = num - blockStart;
		if (r * 50 <= level.halfTicks * den * (50 + level.amount)) {
			uint64_t scale = 50 + level.amount;
			num = blockStart * scale + r * 50;
			den *= scale;
		}
		else {
			uint64_t scale = 50 - level.amount;
			num = blockStart * scale + blockNum * scale - (blockNum - r) * 50;
			den *= scale;
		}
	}
	return num / den;
}

uint64_t swingScaleByPos(uint64_t value, SwingBlockPos pos) {
	uint64_t whole = pos.num / pos.den;
	uint64_t frac = pos.num % pos.den; // < den <= 50^3, so the products below can't overflow for any sane value
	return value * whole + (((value >> 16) * frac / pos.den) << 16) + ((value & 0xFFFF) * frac / pos.den);
}
