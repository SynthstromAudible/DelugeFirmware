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

/// Level 3 of the UI test harness (spec G §4.2): inject a UI script into the real headless app on
/// the deterministic clock and snapshot the panels/pads/LEDs it produced.
///
/// Env:
///   DELUGE_HOST_SCRIPT        the script to run (spec G §2). Enables everything here.
///   DELUGE_HOST_SNAPSHOT_DIR  where `<name>.oled<N>.pbm` / `<name>.state` go (default
///                             `./snapshots`).
///   DELUGE_HOST_DETERMINISTIC required alongside a script; without it the run is not
///                             reproducible and there is no point recording it.
///   DELUGE_SD_ROOT            the card root, as today (host_efatfs_passthrough.cpp).
///
/// SNAPSHOT FORMAT: `tests/harness/snapshot.{h,cpp}` is the source of truth. It is C++ and this
/// file is C, so the P1 and `.state` writers below are a hand port of it and MUST stay
/// byte-identical to it -- the level-2 and level-3 goldens are diffed against each other by spec
/// F's migration acceptance. Read `snapshot.cpp` before touching `write_pbm` or `state_*` here.
///
/// LEVEL-3 DEVIATIONS from the level-2 `.state` (all forced by what the BSP can see, and all
/// checked against the controller's rulings for this task):
///   - no `scene` lines: the legacy UI has no scene names.
///   - LEDs are only ever `led <Name> on`, never `blink`: legacy blinking is the *app* toggling
///     `deluge_control_set_led` on a timer, so the BSP sees a resolved boolean and cannot tell a
///     lit LED from a blinking one caught in its on phase.
///   - `indicator lower|upper …` uses the LEGACY meaning of `which` (0 = mod encoder 0 = the
///     lower gold knob; `gui/views/view.cpp:1403-1468` -> `indicator_leds.cpp:241`, and
///     `hid/encoders.h:80-81` for which knob `mod0` is). The new shell's `IndicatorId` numbers
///     these the other way round; reconciling the two is spec F's job, not this file's.
///   - no `lock` / `notice` lines: neither fact reaches the BSP.

#define _POSIX_C_SOURCE 200809L // mkdir, strdup-free POSIX subset

#include "host_script.h"

#include "harness/ui_script.h" // the parser's extern "C" façade (Task 1)

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

extern uint32_t host_rendered_frames; // host_audio.c — the offline audio clock

// ===========================================================================
// Board tables
//
// Copies of the app's own tables, because the BSP may not include app headers
// (scripts/check_platform_boundary.py). `tests/spec/harness/` cross-checks both against
// `deluge_profile()`, so a table that drifts fails a spec rather than a golden.
// ===========================================================================

typedef struct HostScriptButton {
	const char* name;
	uint8_t x;
	uint8_t y;
} HostScriptButton;

/// The Deluge's 4 × 9 button matrix, row-major, exactly as `src/shell/profiles/deluge.cpp`'s
/// `kMatrix` spells it (which is `src/definitions_cxx.hpp:59-84` in turn). Names are
/// `shell::button_name()`'s (`src/shell/profile.cpp:39-45`). `ButtonId::None` holes -- (4,0) and
/// (4,2) -- carry no button and so are absent.
static const HostScriptButton k_buttons[] = {
    // y = 0
    {"YEnc", 0, 0},
    {"Mod0", 1, 0},
    {"Mod4", 2, 0},
    {"AffectEntire", 3, 0},
    {"Synth", 5, 0},
    {"Scale", 6, 0},
    {"Learn", 7, 0},
    {"Shift", 8, 0},
    // y = 1
    {"XEnc", 0, 1},
    {"Mod1", 1, 1},
    {"Mod5", 2, 1},
    {"Session", 3, 1},
    {"TempoEnc", 4, 1},
    {"Kit", 5, 1},
    {"Load", 6, 1},
    {"Back", 7, 1},
    {"Triplets", 8, 1},
    // y = 2
    {"ModEnc0", 0, 2},
    {"Mod2", 1, 2},
    {"Mod6", 2, 2},
    {"Clip", 3, 2},
    {"Midi", 5, 2},
    {"CrossScreen", 6, 2},
    {"SyncScaling", 7, 2},
    {"Record", 8, 2},
    // y = 3
    {"ModEnc1", 0, 3},
    {"Mod3", 1, 3},
    {"Mod7", 2, 3},
    {"Keyboard", 3, 3},
    {"SelectEnc", 4, 3},
    {"Cv", 5, 3},
    {"Save", 6, 3},
    {"TapTempo", 7, 3},
    {"Play", 8, 3},
};

/// `EncoderRole` name by firmware encoder index -- the index `deluge_encoder_take_edges()` takes.
///
/// `hid/encoder_input.cpp:56-63` fixes the order (`0=scrollY 1=scrollX 2=tempo 3=select 4=mod1
/// 5=mod0`), and `hid/encoders.h:80-81` fixes which gold knob each `mod` is: `mod1` is the UPPER
/// gold encoder and `mod0` the LOWER. So index 4 is `GoldUpper` and index 5 is `GoldLower` -- the
/// opposite of what a name-order reading of the two enums would suggest.
static const char* const k_encoder_roles[6] = {
    "Vertical",   // 0 = scrollY
    "Horizontal", // 1 = scrollX
    "Tempo",      // 2
    "Select",     // 3
    "GoldUpper",  // 4 = mod1 = upper gold
    "GoldLower",  // 5 = mod0 = lower gold
};

/// Quadrature edges the legacy encoder task consumes per detent click.
///
/// `DetentedEncoder::applyEdges` accumulates edges and divides by 2 to get clicks
/// ("Two A-pin edges per quadrature cycle, one quadrature cycle per detent click",
/// `hid/encoders.cpp:28-34`); the gold `ContinuousEncoder`s take edges raw, which matches the
/// same 2 units per detent the hardware produces. host_bsp.c's live GUI bridge already scales by
/// this constant for exactly this reason.
#define HOST_SCRIPT_EDGES_PER_DETENT 2

int ui_script_button_xy(const char* name, uint8_t* x, uint8_t* y) {
	if (name == NULL) {
		return 0;
	}
	for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
		if (strcmp(k_buttons[i].name, name) == 0) {
			if (x != NULL) {
				*x = k_buttons[i].x;
			}
			if (y != NULL) {
				*y = k_buttons[i].y;
			}
			return 1;
		}
	}
	return 0;
}

int ui_script_encoder_index(const char* role) {
	if (role == NULL) {
		return -1;
	}
	for (int i = 0; i < 6; i++) {
		if (strcmp(k_encoder_roles[i], role) == 0) {
			return i;
		}
	}
	return -1;
}

// ===========================================================================
// Snapshot writers — a C port of tests/harness/snapshot.cpp. Keep byte-identical.
// ===========================================================================

/// `mkdir -p`. POSIX-only; this file only ever runs on the host.
/// @return 1 on success.
static int make_dirs(const char* dir) {
	char buf[1024];
	const size_t n = strlen(dir);
	if (n == 0 || n >= sizeof(buf)) {
		return 0;
	}
	memcpy(buf, dir, n + 1);
	for (size_t i = 0; i <= n; i++) {
		if (buf[i] != '/' && buf[i] != '\0') {
			continue;
		}
		if (i == 0) {
			continue; // leading '/' of an absolute path: nothing to create
		}
		const char saved = buf[i];
		buf[i] = '\0';
		if (mkdir(buf, 0755) != 0) {
			struct stat st;
			if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
				return 0;
			}
		}
		buf[i] = saved;
	}
	return 1;
}

static const char* snapshot_dir(void) {
	const char* dir = getenv("DELUGE_HOST_SNAPSHOT_DIR");
	return (dir != NULL && dir[0] != '\0') ? dir : "./snapshots";
}

/// Open `<snapshot_dir>/<name><suffix>` for writing, creating the directory first.
static FILE* open_snapshot_file(const char* name, const char* suffix) {
	const char* dir = snapshot_dir();
	if (!make_dirs(dir)) {
		fprintf(stderr, "[host-script] could not create snapshot directory %s\n", dir);
		return NULL;
	}
	char path[1200];
	if (snprintf(path, sizeof(path), "%s/%s%s", dir, name, suffix) >= (int)sizeof(path)) {
		fprintf(stderr, "[host-script] snapshot path too long: %s/%s%s\n", dir, name, suffix);
		return NULL;
	}
	FILE* f = fopen(path, "wb");
	if (f == NULL) {
		fprintf(stderr, "[host-script] could not write %s\n", path);
	}
	return f;
}

/// Write a page-major 1-bpp framebuffer as a P1 (plain-text) PBM.
///
/// Byte-for-byte `deluge::harness::oled_pbm` (`tests/harness/snapshot.cpp:14-28`): the header
/// `P1\n<w> <h>\n`, then one line per pixel row of `'1'`/`'0'` SEPARATED BY SINGLE SPACES with no
/// trailing space, each line newline-terminated. Bit `b` of `page_major[(y >> 3) * w + x]` is row
/// `(y & 7)` of page `y >> 3`.
static int write_pbm(FILE* f, const uint8_t* page_major, uint16_t w, uint16_t h) {
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

/// Bytes per `.state` line, NUL included — `deluge::harness::kStateLineBytes`
/// (`tests/harness/snapshot.h:36`).
#define HOST_SCRIPT_STATE_LINE_BYTES 96
/// How many `.state` lines one snapshot can hold. The level-2 `StateLines` caps at 256; level 3
/// can legitimately produce more (up to 18 × 8 = 144 pads, 36 LEDs and 2 indicators is already
/// 182, and the pad shadow is 32 columns wide), so this is deliberately roomier. A snapshot that
/// overflowed it would silently lose facts, so `state_addf` records that and the run fails.
#define HOST_SCRIPT_STATE_MAX_LINES 512

typedef struct HostScriptState {
	char lines[HOST_SCRIPT_STATE_MAX_LINES][HOST_SCRIPT_STATE_LINE_BYTES];
	size_t count;
	int overflowed;
} HostScriptState;

static void state_addf(HostScriptState* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/// Format and append one line. Truncated (not rejected) at the line width, exactly as
/// `StateLines::add` does -- see `kStateLineBytes`'s note on why.
static void state_addf(HostScriptState* s, const char* fmt, ...) {
	if (s->count >= HOST_SCRIPT_STATE_MAX_LINES) {
		s->overflowed = 1;
		return;
	}
	va_list args;
	va_start(args, fmt);
	vsnprintf(s->lines[s->count], HOST_SCRIPT_STATE_LINE_BYTES, fmt, args);
	va_end(args);
	s->count++;
}

/// True when \p word is \p s's first whitespace-delimited token
/// (`snapshot.cpp`'s `starts_with_word`).
static int starts_with_word(const char* s, const char* word) {
	const size_t n = strlen(word);
	return strncmp(s, word, n) == 0 && s[n] == ' ';
}

/// `.state` category order, per the controller's ruling on spec G §3 and `snapshot.cpp:47`:
/// `scene` first, then `led`, `pad`, `indicator`, `lock`, `notice`, then anything unrecognised.
/// Level 3 never emits `scene`, `lock` or `notice`, but the order is kept whole so the two
/// levels' files sort identically.
enum HostScriptCategory {
	HOST_SCRIPT_CAT_SCENE = 0,
	HOST_SCRIPT_CAT_LED,
	HOST_SCRIPT_CAT_PAD,
	HOST_SCRIPT_CAT_INDICATOR,
	HOST_SCRIPT_CAT_LOCK,
	HOST_SCRIPT_CAT_NOTICE,
	HOST_SCRIPT_CAT_OTHER,
};

static int category_of(const char* s) {
	if (starts_with_word(s, "scene")) {
		return HOST_SCRIPT_CAT_SCENE;
	}
	if (starts_with_word(s, "led")) {
		return HOST_SCRIPT_CAT_LED;
	}
	if (starts_with_word(s, "pad")) {
		return HOST_SCRIPT_CAT_PAD;
	}
	if (starts_with_word(s, "indicator")) {
		return HOST_SCRIPT_CAT_INDICATOR;
	}
	if (starts_with_word(s, "lock")) {
		return HOST_SCRIPT_CAT_LOCK;
	}
	if (starts_with_word(s, "notice")) {
		return HOST_SCRIPT_CAT_NOTICE;
	}
	return HOST_SCRIPT_CAT_OTHER;
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

static int write_state(FILE* f, HostScriptState* s) {
	qsort(s->lines, s->count, HOST_SCRIPT_STATE_LINE_BYTES, state_line_cmp);
	for (size_t i = 0; i < s->count; i++) {
		fprintf(f, "%s\n", s->lines[i]);
	}
	return ferror(f) == 0;
}

/// The name of the indicator LED at legacy index \p led (`x + y*9`), or NULL when no button --
/// and so no LED -- sits there. Derived from the button matrix rather than a second table:
/// `indicator_leds.h:30-64` builds every `LED` value from its button's own coordinates, so the
/// matrix already is the LED table.
static const char* led_name(uint8_t led) {
	const uint8_t x = (uint8_t)(led % 9u);
	const uint8_t y = (uint8_t)(led / 9u);
	for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
		if (k_buttons[i].x == x && k_buttons[i].y == y) {
			return k_buttons[i].name;
		}
	}
	return NULL;
}

/// Build the `.state` text for the current moment from the BSP's shadows.
static void collect_state(HostScriptState* s) {
	const uint8_t* leds = host_script_led_shadow();
	for (uint8_t i = 0; i < HOST_SCRIPT_LED_COUNT; i++) {
		if (leds[i] == 0) {
			continue; // an LED that is off is not shown at all -- only lit ones are
		}
		const char* name = led_name(i);
		if (name != NULL) {
			state_addf(s, "led %s on", name);
		}
		else {
			// No button at that coordinate: show the raw id, as the level-2 runner's `led_name`
			// does for an `LedId` with no button.
			state_addf(s, "led %u on", (unsigned)i);
		}
	}

	const uint8_t* pads = host_script_pad_shadow();
	for (uint8_t col = 0; col < HOST_SCRIPT_PAD_COLS; col++) {
		for (uint8_t row = 0; row < HOST_SCRIPT_PAD_ROWS; row++) {
			const uint8_t* c = &pads[((size_t)col * HOST_SCRIPT_PAD_ROWS + row) * 3u];
			if (c[0] == 0 && c[1] == 0 && c[2] == 0) {
				continue; // black: skipped -- only non-black pads are ever shown
			}
			state_addf(s, "pad %u %u %u %u %u", (unsigned)col, (unsigned)row, (unsigned)c[0], (unsigned)c[1],
			           (unsigned)c[2]);
		}
	}

	for (uint8_t which = 0; which < HOST_SCRIPT_RINGS; which++) {
		const uint8_t* levels = host_script_ring_shadow(which);
		if (levels == NULL) {
			continue; // never written: reporting 0 0 0 0 would invent a fact the app never stated
		}
		// which == 0 is mod encoder 0, the LOWER gold knob -- see this file's header comment.
		state_addf(s, "indicator %s %u %u %u %u", which == 0 ? "lower" : "upper", (unsigned)levels[0],
		           (unsigned)levels[1], (unsigned)levels[2], (unsigned)levels[3]);
	}
}

// ===========================================================================
// The script runner
// ===========================================================================

/// Snapshot names asked for by steps applied in the current pump call, written at the end of it.
/// Bounded by the parser's own 256-step cap, so a script cannot ask for more than this.
#define HOST_SCRIPT_MAX_PENDING 256

static int script_loaded;
static char pending_snapshots[HOST_SCRIPT_MAX_PENDING][32];
static size_t pending_count;

/// A step popped during `host_script_init`'s `board` scan that turned out not to be a `board`.
/// The façade consumes what it hands back, so the first non-`board` step is held here and
/// replayed by the pump.
static UiScriptStep stashed_step;
static int have_stashed;

/// Bail out the way a test harness should: say what is wrong, on stderr, and exit 2.
static void script_fail(const char* what) __attribute__((noreturn));

static void script_fail(const char* what) {
	fprintf(stderr, "[host-script] %s\n", what);
	fflush(NULL);
	exit(2);
}

static void write_snapshot(const char* name) {
	const uint8_t count = host_script_display_count();
	for (uint8_t i = 0; i < count; i++) {
		uint16_t w = 0;
		uint16_t h = 0;
		const uint8_t* frame = host_script_oled_shadow(i, &w, &h);
		if (frame == NULL || w == 0 || h == 0) {
			// Nothing has been blitted to this panel yet -- the snapshot is earlier than the app's
			// first frame. Recording an all-black `.pbm` would give a golden that cannot tell
			// "blank screen" from "never drawn", and writing nothing at all would record a golden
			// set with the file simply absent, which then passes forever. So: fail the run and say
			// which panel and when. Move the snapshot later in the script.
			fprintf(stderr, "[host-script] snapshot \"%s\": panel %u has never blitted (nothing drawn yet)\n", name,
			        (unsigned)i);
			script_fail("snapshot taken before the app's first frame");
		}
		char suffix[24];
		snprintf(suffix, sizeof(suffix), ".oled%u.pbm", (unsigned)i);
		FILE* f = open_snapshot_file(name, suffix);
		if (f == NULL) {
			script_fail("could not open a snapshot .pbm for writing");
		}
		const int ok = write_pbm(f, frame, w, h);
		if (fclose(f) != 0 || !ok) {
			script_fail("could not write a snapshot .pbm");
		}
	}

	static HostScriptState state; // ~48 KiB; static so a deep call stack does not carry it
	memset(&state, 0, sizeof(state));
	collect_state(&state);
	if (state.overflowed) {
		script_fail("a .state snapshot needed more lines than HOST_SCRIPT_STATE_MAX_LINES");
	}
	FILE* f = open_snapshot_file(name, ".state");
	if (f == NULL) {
		script_fail("could not open a snapshot .state for writing");
	}
	const int ok = write_state(f, &state);
	if (fclose(f) != 0 || !ok) {
		script_fail("could not write a snapshot .state");
	}
}

void host_script_write_pending(void) {
	for (size_t i = 0; i < pending_count; i++) {
		write_snapshot(pending_snapshots[i]);
	}
	pending_count = 0;
}

/// Apply one step. Returns 0 when the step was `quit` (the caller flushes and exits).
static int apply_step(const UiScriptStep* step) {
	switch ((UiScriptStepKind)step->kind) {
	case UI_SCRIPT_BOARD:
		// Applied at init, before `deluge_board()` is first read. Reaching one here means it was
		// not at 0 ms, which `validate_script` already rejected.
		script_fail("board is only allowed at 0 ms");

	case UI_SCRIPT_SNAPSHOT:
		if (pending_count >= HOST_SCRIPT_MAX_PENDING) {
			script_fail("too many snapshots due at once");
		}
		snprintf(pending_snapshots[pending_count], sizeof(pending_snapshots[0]), "%s", step->name);
		pending_count++;
		break;

	case UI_SCRIPT_BUTTON: {
		uint8_t x = 0;
		uint8_t y = 0;
		if (!ui_script_button_xy(step->name, &x, &y)) {
			script_fail("unknown button"); // already rejected at load; belt and braces
		}
		host_script_push_event(DELUGE_EVENT_BUTTON, x, y, step->down ? 255 : 0);
		break;
	}

	case UI_SCRIPT_ENCODER: {
		const int index = ui_script_encoder_index(step->name);
		if (index < 0) {
			script_fail("unknown encoder role");
		}
		// Computed wide and range-checked, never narrowed on faith: the accumulator is an
		// `int8_t`, so at 2 edges/detent a +64 would wrap to -128 and turn a script asking for 64
		// detents clockwise into 64 anticlockwise (the same reasoning as the level-2 runner's).
		const int32_t edges = step->a * (int32_t)HOST_SCRIPT_EDGES_PER_DETENT;
		if (edges < -128 || edges > 127) {
			script_fail("too many detents for one encoder step");
		}
		host_script_add_encoder_edges((uint8_t)index, edges);
		break;
	}

	case UI_SCRIPT_PAD:
		// `step->down` is deliberately unread: the parser folds `up` into velocity 0, which is
		// exactly what `DelugeInputEvent::value` means for a pad.
		host_script_push_event(DELUGE_EVENT_PAD, (uint8_t)step->a, (uint8_t)step->b, (int16_t)step->c);
		break;

	case UI_SCRIPT_RELEASED:
		host_script_push_event(DELUGE_EVENT_NO_PRESSES, 0, 0, 0);
		break;

	case UI_SCRIPT_MODEL:
	case UI_SCRIPT_NOTICE:
		script_fail("shell-level command in an app script"); // already rejected at load

	case UI_SCRIPT_QUIT:
		return 0;
	}
	return 1;
}

void host_script_pump(void) {
	if (!script_loaded) {
		return;
	}
	// The deterministic clock, in milliseconds: host_bsp.c:104-153 drives every app clock off the
	// same rendered-frame count, so the script's timeline and the app's are the same timeline.
	const uint32_t now_ms = (uint32_t)((uint64_t)host_rendered_frames * 1000u / 44100u);

	int quit = 0;
	// Every step that is due, in order, on every call -- not one per call. A pump that applied
	// one step per poll would make the run's behaviour depend on how often the app happens to
	// poll, which is exactly the nondeterminism this harness exists to exclude.
	for (;;) {
		UiScriptStep step;
		if (have_stashed) {
			if (stashed_step.at_ms > now_ms) {
				break;
			}
			step = stashed_step;
			have_stashed = 0;
		}
		else if (!ui_script_next_due(now_ms, &step)) {
			break;
		}
		if (!apply_step(&step)) {
			quit = 1;
			break;
		}
	}

	host_script_write_pending();

	if (quit) {
		fprintf(stderr, "[host-script] quit at %u ms (%u frames)\n", now_ms, host_rendered_frames);
		fflush(NULL);
		quick_exit(0);
	}
}

bool host_script_active(void) {
	return script_loaded != 0;
}

/// Walk the whole script once, rejecting anything level 3 cannot honour, then reload it to put
/// the façade's cursor back at the start. Cheaper than teaching the façade to rewind, and it
/// keeps every "your script is wrong" message at load time rather than mid-run.
static void validate_script(const char* path) {
	for (;;) {
		UiScriptStep step;
		if (!ui_script_next_due(UINT32_MAX, &step)) {
			break;
		}
		switch ((UiScriptStepKind)step.kind) {
		case UI_SCRIPT_MODEL:
		case UI_SCRIPT_NOTICE:
			// `model` raises a `notify::Dirty` and `notice` pushes a `notify::Notice`; both are
			// shell-level channels the legacy app does not have (spec G §4, levels 2 vs 3).
			script_fail("shell-level command in an app script");
		case UI_SCRIPT_BOARD:
			if (step.at_ms != 0) {
				script_fail("board is only allowed at 0 ms");
			}
			break;
		case UI_SCRIPT_BUTTON:
			if (!ui_script_button_xy(step.name, NULL, NULL)) {
				fprintf(stderr, "[host-script] unknown button \"%s\"\n", step.name);
				script_fail("unknown button");
			}
			break;
		case UI_SCRIPT_ENCODER:
			if (ui_script_encoder_index(step.name) < 0) {
				fprintf(stderr, "[host-script] unknown encoder role \"%s\"\n", step.name);
				script_fail("unknown encoder role");
			}
			break;
		case UI_SCRIPT_SNAPSHOT:
		case UI_SCRIPT_PAD:
		case UI_SCRIPT_RELEASED:
		case UI_SCRIPT_QUIT:
			break;
		}
	}
	if (ui_script_load(path) != 0) {
		script_fail("script became unreadable between passes");
	}
}

/// Apply the `board` steps at the head of the script, and stash the first step that is not one.
///
/// A `board` step rewrites the board descriptor, which is latched by whoever reads it first, so
/// it cannot wait for the pump -- by then the app has been built around the old descriptor.
static void apply_leading_board_steps(void) {
	for (;;) {
		UiScriptStep step;
		if (!ui_script_next_due(0, &step)) {
			return;
		}
		if ((UiScriptStepKind)step.kind != UI_SCRIPT_BOARD) {
			stashed_step = step;
			have_stashed = 1;
			return;
		}
		const int rc = host_script_set_displays(step.text);
		if (rc == -1) {
			fprintf(stderr, "[host-script] unparseable board spec \"%s\"\n", step.text);
			script_fail("unparseable board spec");
		}
		if (rc == -2) {
			script_fail("board came too late: the board descriptor has already been read");
		}
	}
}

/// True when \p name ends in \p suffix.
static int ends_with(const char* name, const char* suffix) {
	const size_t n = strlen(name);
	const size_t m = strlen(suffix);
	return n >= m && strcmp(name + n - m, suffix) == 0;
}

/// Remove the `.pbm`/`.state` files already in the snapshot directory.
///
/// Without this, a snapshot renamed in the script leaves its old file behind forever, and the
/// differ reports it as "extra in got" on every run afterwards -- the level-2 runner clears its
/// `got/` directory for exactly this reason. Deliberately narrow: only those two suffixes, only
/// the named directory, never recursive, so pointing DELUGE_HOST_SNAPSHOT_DIR somewhere careless
/// cannot take anything else with it.
static void clear_stale_snapshots(void) {
	const char* dir = snapshot_dir();
	DIR* d = opendir(dir);
	if (d == NULL) {
		return; // not there yet: nothing stale to clear
	}
	const struct dirent* e;
	while ((e = readdir(d)) != NULL) {
		if (!ends_with(e->d_name, ".pbm") && !ends_with(e->d_name, ".state")) {
			continue;
		}
		char path[1200];
		if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) < (int)sizeof(path)) {
			remove(path);
		}
	}
	closedir(d);
}

/// The audio capture owns the process's lifetime (host_audio.c finalises the WAV and
/// `quick_exit`s after `DELUGE_HOST_CAPTURE_SECONDS`, default 2.0 s). A script's `quit` must be
/// what ends the run instead, so stretch the capture past the script's last step and send its
/// WAV to /dev/null -- unless the caller asked for either explicitly, in which case they meant it.
///
/// Guessing the script's end from the file is not possible through the façade (it consumes what
/// it reads), so this is simply generous: an hour of capture, which no UI script approaches.
static void defer_capture_exit(void) {
	if (getenv("DELUGE_HOST_CAPTURE_SECONDS") == NULL) {
		setenv("DELUGE_HOST_CAPTURE_SECONDS", "3600", 0);
	}
	if (getenv("DELUGE_HOST_WAV") == NULL) {
		setenv("DELUGE_HOST_WAV", "/dev/null", 0);
	}
}

void host_script_init(void) {
	const char* path = getenv("DELUGE_HOST_SCRIPT");
	if (path == NULL || path[0] == '\0') {
		return;
	}
	// Same presence test host_bsp.c's `host_deterministic()` uses, so the two can never disagree
	// about whether this run is on the frame clock.
	if (getenv("DELUGE_HOST_DETERMINISTIC") == NULL) {
		script_fail("DELUGE_HOST_SCRIPT requires DELUGE_HOST_DETERMINISTIC=1 "
		            "(the script's clock is the rendered-frame clock)");
	}

	const int rc = ui_script_load(path);
	if (rc < 0) {
		fprintf(stderr, "[host-script] could not read script %s\n", path);
		script_fail("unreadable script");
	}
	if (rc > 0) {
		fprintf(stderr, "[host-script] %s:%d: parse error\n", path, rc);
		script_fail("unparseable script");
	}

	validate_script(path);
	apply_leading_board_steps();
	clear_stale_snapshots();
	defer_capture_exit();
	script_loaded = 1;
	fprintf(stderr, "[host-script] running %s -> %s\n", path, snapshot_dir());
}
