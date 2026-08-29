/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// See `host_snapshot.h` for why this is its own translation unit: it must link into both the
/// host BSP and `deluge_spec` (`tests/spec/harness/identity_spec.cpp`'s cross-check against
/// `tests/harness/snapshot.cpp`), and `host_script.c` proper cannot -- it pulls in BSP statics.
/// This file depends on nothing beyond the C standard library and `harness/ui_script.h`'s shared
/// `UI_SCRIPT_STATE_LINE_BYTES` constant.

#include "host_snapshot.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int host_snapshot_write_pbm(FILE* f, const uint8_t* page_major, uint16_t w, uint16_t h) {
	fprintf(f, "P1\n%u %u\n", (unsigned)w, (unsigned)h);
	for (uint16_t y = 0; y < h; y++) {
		for (uint16_t x = 0; x < w; x++) {
			const int on = (page_major[(y >> 3) * w + x] & (1u << (y & 7))) != 0;
			fputc(on ? '1' : '0', f);
			if (x + 1 < w) {
				fputc(' ', f);
			}
		}
		fputc('\n', f);
	}
	return ferror(f) == 0;
}

void host_snapshot_reset(HostSnapshotLines* s) {
	memset(s, 0, sizeof(*s));
}

void host_snapshot_addf(HostSnapshotLines* s, const char* fmt, ...) {
	if (s->count >= HOST_SNAPSHOT_MAX_LINES) {
		s->overflowed = 1;
		return;
	}
	va_list args;
	va_start(args, fmt);
	vsnprintf(s->lines[s->count], UI_SCRIPT_STATE_LINE_BYTES, fmt, args);
	va_end(args);
	s->count++;
}

void host_snapshot_add_pad(HostSnapshotLines* s, uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
	if (r == 0 && g == 0 && b == 0) {
		return; // black: skipped -- only non-black pads are ever shown
	}
	host_snapshot_addf(s, "pad %u %u %u %u %u", (unsigned)x, (unsigned)y, (unsigned)r, (unsigned)g, (unsigned)b);
}

void host_snapshot_add_led(HostSnapshotLines* s, const char* name, int blink, uint16_t period_ms) {
	if (blink) {
		host_snapshot_addf(s, "led %s blink %u", name, (unsigned)period_ms);
	}
	else {
		host_snapshot_addf(s, "led %s on", name);
	}
}

void host_snapshot_add_indicator(HostSnapshotLines* s, int upper, const uint8_t levels[4]) {
	host_snapshot_addf(s, "indicator %s %u %u %u %u", upper ? "upper" : "lower", (unsigned)levels[0],
	                   (unsigned)levels[1], (unsigned)levels[2], (unsigned)levels[3]);
}

/// True when \p word is \p s's first whitespace-delimited token (`snapshot.cpp`'s
/// `starts_with_word`).
static int starts_with_word(const char* s, const char* word) {
	const size_t n = strlen(word);
	return strncmp(s, word, n) == 0 && s[n] == ' ';
}

/// `.state` category order, per the controller's ruling on spec G §3 and `snapshot.cpp:47`:
/// `scene` first, then `led`, `pad`, `indicator`, `lock`, `notice`, then anything unrecognised.
/// Level 3 never emits `scene`, `lock` or `notice`, but the order is kept whole so the two
/// levels' files sort identically.
enum HostSnapshotCategory {
	HOST_SNAPSHOT_CAT_SCENE = 0,
	HOST_SNAPSHOT_CAT_LED,
	HOST_SNAPSHOT_CAT_PAD,
	HOST_SNAPSHOT_CAT_INDICATOR,
	HOST_SNAPSHOT_CAT_LOCK,
	HOST_SNAPSHOT_CAT_NOTICE,
	HOST_SNAPSHOT_CAT_OTHER,
};

static int category_of(const char* s) {
	if (starts_with_word(s, "scene")) {
		return HOST_SNAPSHOT_CAT_SCENE;
	}
	if (starts_with_word(s, "led")) {
		return HOST_SNAPSHOT_CAT_LED;
	}
	if (starts_with_word(s, "pad")) {
		return HOST_SNAPSHOT_CAT_PAD;
	}
	if (starts_with_word(s, "indicator")) {
		return HOST_SNAPSHOT_CAT_INDICATOR;
	}
	if (starts_with_word(s, "lock")) {
		return HOST_SNAPSHOT_CAT_LOCK;
	}
	if (starts_with_word(s, "notice")) {
		return HOST_SNAPSHOT_CAT_NOTICE;
	}
	return HOST_SNAPSHOT_CAT_OTHER;
}

/// Sort by category, then ASCII within it. `scene` keeps insertion order in `snapshot.cpp`; level
/// 3 emits none, so a total order here is equivalent and needs no stable sort.
static int state_line_cmp(const void* a, const void* b) {
	const char* la = (const char*)a;
	const char* lb = (const char*)b;
	const int ca = category_of(la);
	const int cb = category_of(lb);
	if (ca != cb) {
		return ca < cb ? -1 : 1;
	}
	return strcmp(la, lb);
}

int host_snapshot_write_state(FILE* f, HostSnapshotLines* s) {
	qsort(s->lines, s->count, UI_SCRIPT_STATE_LINE_BYTES, state_line_cmp);
	for (size_t i = 0; i < s->count; i++) {
		fprintf(f, "%s\n", s->lines[i]);
	}
	return ferror(f) == 0;
}
