/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#pragma once
#include <cstdint>

// Parse BPM from sample filename. Handles both explicit "120BPM" and bare numbers
// like "drums_120_loop.wav" with sanity checking (range 40-300).
// Returns 0 if no valid BPM found.

static constexpr int32_t kMinPlausibleBPM = 40;
static constexpr int32_t kMaxPlausibleBPM = 300;

inline int32_t extractBPMNumber(const char* start, const char* end) {
	int32_t val = 0;
	for (const char* d = start; d < end; d++) {
		val = val * 10 + (*d - '0');
	}
	return val;
}

inline int32_t parseBPMFromFilename(const char* path) {
	if (!path || !*path) {
		return 0;
	}

	// Find the filename portion (after last '/')
	const char* filename = path;
	for (const char* p = path; *p; p++) {
		if (*p == '/') {
			filename = p + 1;
		}
	}

	// First pass: look for explicit [digits]BPM pattern (highest confidence)
	for (const char* p = filename; *p; p++) {
		if ((p[0] == 'B' || p[0] == 'b') && (p[1] == 'P' || p[1] == 'p') && (p[2] == 'M' || p[2] == 'm')
		    && (p[3] == '.' || p[3] == '_' || p[3] == '-' || p[3] == ' ' || p[3] == '\0')) {
			const char* end = p;
			const char* start = p;
			while (start > filename && start[-1] >= '0' && start[-1] <= '9') {
				start--;
			}
			if (start < end) {
				int32_t bpm = extractBPMNumber(start, end);
				if (bpm >= kMinPlausibleBPM && bpm <= kMaxPlausibleBPM) {
					return bpm;
				}
			}
		}
	}

	// Second pass: look for bare numbers delimited by separators (_, -, space, or start/end)
	// that fall in a plausible BPM range
	for (const char* p = filename; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			// Check that this number is at a word boundary (start of filename or after separator)
			if (p > filename && p[-1] != '_' && p[-1] != '-' && p[-1] != ' ') {
				// Skip this digit sequence — it's embedded in a word
				while (*p >= '0' && *p <= '9') {
					p++;
				}
				if (!*p) {
					break;
				}
				p--; // Compensate for the for-loop's p++ so we don't skip the next char
				continue;
			}
			const char* start = p;
			while (*p >= '0' && *p <= '9') {
				p++;
			}
			// Check trailing boundary (separator, dot/extension, or end)
			if (*p == '_' || *p == '-' || *p == ' ' || *p == '.' || *p == '\0') {
				int32_t val = extractBPMNumber(start, p);
				if (val >= kMinPlausibleBPM && val <= kMaxPlausibleBPM) {
					return val;
				}
			}
			if (!*p) {
				break;
			}
		}
	}
	return 0;
}
