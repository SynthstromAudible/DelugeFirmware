/*
 * Copyright (c) 2024-2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "util/sample_velocity_parse.h"

namespace {
char toLowerAscii(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

bool isAsciiAlpha(char c) {
	char lower = toLowerAscii(c);
	return lower >= 'a' && lower <= 'z';
}

bool isSeparator(char c) {
	return c == '_' || c == '-' || c == ' ';
}
} // namespace

bool tryParseVelocityFromFilename(char const* filename, uint8_t* outVelocity) {
	bool found = false;
	uint8_t foundValue = 0;

	for (char const* p = filename; *p != '\0'; p++) {
		if (toLowerAscii(p[0]) != 'v' || toLowerAscii(p[1]) != 'e' || toLowerAscii(p[2]) != 'l') {
			continue;
		}

		char const* q = p + 3;
		while (isAsciiAlpha(*q)) {
			q++;
		}
		while (isSeparator(*q)) {
			q++;
		}

		if (*q < '0' || *q > '9') {
			continue;
		}

		int32_t value = 0;
		int32_t numDigits = 0;
		while (*q >= '0' && *q <= '9' && numDigits < 3) {
			value = value * 10 + (*q - '0');
			q++;
			numDigits++;
		}
		// A 4th consecutive digit means this is a longer number than a plausible velocity value
		// (e.g. part of a date or catalogue number) - reject it rather than truncate.
		if (*q >= '0' && *q <= '9') {
			continue;
		}

		if (value >= 1 && value <= 127) {
			found = true;
			foundValue = (uint8_t)value;
			// Keep scanning - a later "vel..." token in the same filename wins.
		}
	}

	if (found) {
		*outVelocity = foundValue;
	}
	return found;
}

void computeVelocityLayerBoundaries(const uint8_t* velocities, int32_t count, uint8_t* outMin, uint8_t* outMax) {
	if (count <= 1) {
		return;
	}

	// count is at most kMaxRoundRobinAlternates + 1 (4) - a plain insertion sort is simplest and
	// plenty fast for an array this small.
	int32_t order[kMaxRoundRobinAlternates + 1];
	for (int32_t i = 0; i < count; i++) {
		order[i] = i;
	}
	for (int32_t i = 1; i < count; i++) {
		int32_t key = order[i];
		int32_t j = i - 1;
		while (j >= 0 && velocities[order[j]] > velocities[key]) {
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = key;
	}

	for (int32_t rank = 0; rank < count; rank++) {
		int32_t memberIndex = order[rank];
		outMin[memberIndex] = (rank == 0) ? MultisampleRange::kDefaultVelocityMin
		                                  : (uint8_t)((velocities[order[rank - 1]] + velocities[memberIndex]) / 2 + 1);
		outMax[memberIndex] = (rank == count - 1)
		                          ? MultisampleRange::kDefaultVelocityMax
		                          : (uint8_t)((velocities[memberIndex] + velocities[order[rank + 1]]) / 2);
	}
}
