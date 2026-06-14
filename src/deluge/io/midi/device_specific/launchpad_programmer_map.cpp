/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_programmer_map.h"

namespace launchpad_programmer_map {

// Standard Novation Programmer layout: top row 81–88, bottom row 11–18.
static constexpr uint8_t kGridNotes[8][8] = {
    {81, 82, 83, 84, 85, 86, 87, 88}, {71, 72, 73, 74, 75, 76, 77, 78}, {61, 62, 63, 64, 65, 66, 67, 68},
    {51, 52, 53, 54, 55, 56, 57, 58}, {41, 42, 43, 44, 45, 46, 47, 48}, {31, 32, 33, 34, 35, 36, 37, 38},
    {21, 22, 23, 24, 25, 26, 27, 28}, {11, 12, 13, 14, 15, 16, 17, 18},
};

uint8_t gridLedIndex(int32_t x, int32_t y) {
	return gridNote(x, y);
}

uint8_t gridNote(int32_t x, int32_t y) {
	if (x < 0 || x >= 8 || y < 0 || y >= 8) {
		return 0;
	}
	return kGridNotes[y][x];
}

uint8_t gridLedIndexFromDeluge(int32_t x, int32_t delugeY) {
	return gridLedIndex(x, 7 - delugeY);
}

bool delugeGridCoordsFromNote(uint8_t note, int32_t& x, int32_t& y) {
	for (int32_t lpY = 0; lpY < 8; lpY++) {
		for (int32_t lpX = 0; lpX < 8; lpX++) {
			if (kGridNotes[lpY][lpX] == note) {
				x = lpX;
				y = 7 - lpY;
				return true;
			}
		}
	}
	return false;
}

// Top row (90–98) and Novation logo (99). Scene column (19–89) is lit from section colours.
static constexpr uint8_t kNonGridLedIndices[kNonGridLedIndexCount] = {
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
};

static constexpr uint8_t kFixedOffLedIndices[kFixedOffLedIndexCount] = {
    90, 91, 92, 93, 94, 99,
};

uint8_t const* nonGridLedIndices() {
	return kNonGridLedIndices;
}

uint8_t const* fixedOffLedIndices() {
	return kFixedOffLedIndices;
}

bool sceneRowFromCc(uint8_t cc, int32_t& delugeY) {
	for (int32_t i = 0; i < 8; i++) {
		if (kSceneCcs[i] == cc) {
			// kSceneCcs[0] = top Launchpad button; Deluge y = 7 is top row.
			delugeY = 7 - i;
			return true;
		}
	}
	return false;
}

uint8_t sceneLedIndexForDelugeRow(int32_t delugeY) {
	if (delugeY < 0 || delugeY >= 8) {
		return 0;
	}
	return kSceneCcs[7 - delugeY];
}

} // namespace launchpad_programmer_map
