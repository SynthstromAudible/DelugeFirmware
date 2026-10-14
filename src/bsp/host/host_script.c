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
/// SNAPSHOT FORMAT: `tests/harness/snapshot.{h,cpp}` is the source of truth. It is C++, so the P1
/// and `.state` writers are a hand port of it into C -- `host_snapshot.{h,c}`, split out (final
/// review I-3) exactly as `host_script_tables.c` splits out the name tables, so BOTH this BSP and
/// `deluge_spec` can compile them and `tests/spec/harness/identity_spec.cpp` can cross-check the
/// two writers against one fact set instead of leaning on a comment. This file only ever calls
/// into that API (`collect_state`, below) -- read `host_snapshot.h` before touching the format
/// itself.
///
/// LEVEL-3 DEVIATIONS from the level-2 `.state` (all forced by what the BSP can see, and all
/// checked against the controller's rulings for this task):
///   - no `scene` lines: the legacy UI has no scene names.
///   - LEDs are only ever `led <Name> on`, never `blink`: legacy blinking is the *app* toggling
///     `deluge_control_set_led` on a timer, so the BSP sees a resolved boolean and cannot tell a
///     lit LED from a blinking one caught in its on phase.
///   - `indicator lower|upper …` uses the meaning of `which` at the boundary: 0 = mod encoder 0 =
///     the lower gold knob (`gui/views/view.cpp:1420-1485` -> `indicator_leds.cpp:241`, with
///     `hid/encoders.h:80-81` for which knob `mod0` is). The new shell's `IndicatorId` is numbered
///     `{Lower = 0, Upper = 1}` to match, so both levels name the same physical knob the same way.
///   - no `lock` / `notice` lines: neither fact reaches the BSP.

#define _POSIX_C_SOURCE 200809L // mkdir, strdup-free POSIX subset

#include "host_script.h"

#include "harness/ui_script.h" // the parser's extern "C" façade (Task 1)
#include "host_snapshot.h"     // the .pbm/.state writers (final review I-3)

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> // _exit, from inside the at_quick_exit deadline hook

extern uint32_t host_rendered_frames; // host_audio.c — the offline audio clock

// ===========================================================================
// Board tables
//
// The button-matrix and encoder-order name tables (and `ui_script_button_xy` /
// `ui_script_encoder_index`) live in `host_script_tables.c` (included via `host_script.h`), not
// here: that pair is dependency-free and compiles into both the host BSP and `deluge_spec`, so
// `tests/spec/harness/tables_spec.cpp` can cross-check it against `deluge_profile()` without
// linking this file's BSP statics. See `host_script_tables.h`.
// ===========================================================================

/// Quadrature edges the legacy encoder task consumes per detent click.
///
/// `DetentedEncoder::applyEdges` accumulates edges and divides by 2 to get clicks
/// ("Two A-pin edges per quadrature cycle, one quadrature cycle per detent click",
/// `hid/encoders.cpp:28-34`); the gold `ContinuousEncoder`s take edges raw, which matches the
/// same 2 units per detent the hardware produces. host_bsp.c's live GUI bridge already scales by
/// this constant for exactly this reason.
#define HOST_SCRIPT_EDGES_PER_DETENT 2

// ===========================================================================
// Snapshot writers — host_snapshot.{h,c}. See that header and this file's own header comment.
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

/// The name of the indicator LED at legacy index \p led (`x + y*9`), or NULL when no button --
/// and so no LED -- sits there. Derived from the button matrix rather than a second table:
/// `indicator_leds.h:30-64` builds every `LED` value from its button's own coordinates, so the
/// matrix already is the LED table. `ui_script_button_name_at` is `host_script_tables.c`'s reverse
/// of `ui_script_button_xy`, over that same table.
static const char* led_name(uint8_t led) {
	const uint8_t x = (uint8_t)(led % 9u);
	const uint8_t y = (uint8_t)(led / 9u);
	return ui_script_button_name_at(x, y);
}

/// Build the `.state` text for the current moment from the BSP's shadows.
static void collect_state(HostSnapshotLines* s) {
	const uint8_t* leds = host_script_led_shadow();
	for (uint8_t i = 0; i < HOST_SCRIPT_LED_COUNT; i++) {
		if (leds[i] == 0) {
			continue; // an LED that is off is not shown at all -- only lit ones are
		}
		const char* name = led_name(i);
		char id_buf[8];
		if (name == NULL) {
			// No button at that coordinate: show the raw legacy `x + y*9` id instead -- the same
			// "nothing to name it by" fallback the level-2 runner's `led_name` uses for an `LedId`
			// with no button, just in level 2's own number space (the `LedId`/`ButtonId` value)
			// rather than this one (`x + y*9`). Unreachable on the Deluge board (every matrix
			// position that carries an LED has a name), so the two number spaces never have to
			// agree in practice.
			snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)i);
			name = id_buf;
		}
		host_snapshot_add_led(s, name, 0, 0);
	}

	const uint8_t* pads = host_script_pad_shadow();
	for (uint8_t col = 0; col < HOST_SCRIPT_PAD_COLS; col++) {
		for (uint8_t row = 0; row < HOST_SCRIPT_PAD_ROWS; row++) {
			const uint8_t* c = &pads[((size_t)col * HOST_SCRIPT_PAD_ROWS + row) * 3u];
			host_snapshot_add_pad(s, col, row, c[0], c[1], c[2]);
		}
	}

	for (uint8_t which = 0; which < HOST_SCRIPT_RINGS; which++) {
		const uint8_t* levels = host_script_ring_shadow(which);
		if (levels == NULL) {
			continue; // never written: reporting 0 0 0 0 would invent a fact the app never stated
		}
		// which == 0 is mod encoder 0, the LOWER gold knob -- see this file's header comment, and
		// `IndicatorId` (`src/ui/core/led_frame.h`), which is numbered to agree.
		host_snapshot_add_indicator(s, which != 0, levels);
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

/// The largest `at_ms` in the loaded script, filled in by `validate_script`. Both deadlines below
/// are derived from it: a script cannot legitimately need time past its own last step.
static uint32_t script_last_at_ms;

/// Set just before the run's own `quick_exit(0)`, so the deadline hook can tell an orderly `quit`
/// from the capture running out underneath a script that never reached one.
static int script_finished;

/// How much RENDERED AUDIO time past the script's last step the capture is allowed to run before
/// the process is declared stuck.
///
/// Small on purpose: a script's `quit` fires within a render block of its own last step, so five
/// audio seconds is slack, not budget.
///
/// This deadline must always beat the `ui_app_scripts_<name>` ctest `TIMEOUT`, because this one
/// carries the diagnosis ("the app stopped reaching its `quit`") and ctest's would replace it with
/// a bare "Timeout". The two are in DIFFERENT UNITS -- this counts audio seconds
/// (`DELUGE_HOST_CAPTURE_SECONDS`), ctest counts wall seconds -- and the sim renders slower than
/// real time (~0.26x on a `-DDELUGE_SIM_X64=ON` build), so the ordering is not automatic. It is
/// `sim/CMakeLists.txt` that guarantees it, by sizing its timeout as `8 * (last_s + 5) + 30` wall
/// seconds: this deadline converted at a render rate as slow as 0.125x. Change the 5 here and that
/// expression has to move with it.
#define HOST_SCRIPT_DEADLINE_MARGIN_S 5u

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
		const int ok = host_snapshot_write_pbm(f, frame, w, h);
		if (fclose(f) != 0 || !ok) {
			script_fail("could not write a snapshot .pbm");
		}
	}

	static HostSnapshotLines state; // ~48 KiB; static so a deep call stack does not carry it
	host_snapshot_reset(&state);
	collect_state(&state);
	if (state.overflowed) {
		script_fail("a .state snapshot needed more lines than HOST_SNAPSHOT_MAX_LINES");
	}
	FILE* f = open_snapshot_file(name, ".state");
	if (f == NULL) {
		script_fail("could not open a snapshot .state for writing");
	}
	const int ok = host_snapshot_write_state(f, &state);
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
		script_finished = 1;
		fflush(NULL);
		quick_exit(0);
	}
}

bool host_script_active(void) {
	return script_loaded != 0;
}

/// Walk the whole script once -- rejecting anything level 3 cannot honour and recording the last
/// `at_ms` for the deadline -- then rewind the façade's cursor. Every "your script is wrong"
/// message belongs here, at load, rather than mid-run with the app already booted.
static void validate_script(void) {
	uint8_t pad_cols = 0;
	uint8_t pad_rows = 0;
	host_script_pad_bounds(&pad_cols, &pad_rows);

	int seen_non_board = 0;
	for (;;) {
		UiScriptStep step;
		if (!ui_script_next_due(UINT32_MAX, &step)) {
			break;
		}
		if (step.at_ms > script_last_at_ms) {
			script_last_at_ms = step.at_ms;
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
			// Position, not just time: `board` is applied at init, before `deluge_board()` latches
			// the descriptor, so only a `board` at the very head of the script can be honoured.
			// Rejecting it here -- rather than letting the pump reach it -- is also the difference
			// between an accurate message and "board is only allowed at 0 ms" for a step that
			// plainly is at 0 ms.
			if (seen_non_board) {
				script_fail("board must come before every other step");
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
		case UI_SCRIPT_PAD:
			// A typo'd coordinate should be a load-time error here, exactly as a typo'd button
			// name is -- not an out-of-range press handed to the app's matrix handler to do
			// something undefined with. (The live GUI path forwards wire coordinates unchecked,
			// but a script is authored, not received.)
			if (step.a < 0 || step.a >= pad_cols || step.b < 0 || step.b >= pad_rows) {
				fprintf(stderr, "[host-script] pad %d %d is off the %ux%u grid\n", step.a, step.b, (unsigned)pad_cols,
				        (unsigned)pad_rows);
				script_fail("pad coordinate off the grid");
			}
			break;
		case UI_SCRIPT_SNAPSHOT:
		case UI_SCRIPT_RELEASED:
		case UI_SCRIPT_QUIT:
			break;
		}
		if ((UiScriptStepKind)step.kind != UI_SCRIPT_BOARD) {
			seen_non_board = 1;
		}
	}
	ui_script_rewind();
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

/// The capture deadline expired with the script still running: say so and fail, rather than let
/// host_audio.c's `quick_exit(0)` report success for a run that produced no `quit`.
///
/// Registered with `at_quick_exit`, so it sees every `quick_exit` the process makes -- the
/// capture's, and the script's own. `script_finished` distinguishes them.
static void deadline_hook(void) {
	if (!script_loaded || script_finished) {
		return;
	}
	fprintf(stderr,
	        "[host-script] the capture deadline expired at %u ms with the script still running "
	        "(last step is at %u ms). The app stopped reaching its `quit` -- see the snapshots "
	        "written so far.\n",
	        (unsigned)((uint64_t)host_rendered_frames * 1000u / 44100u), script_last_at_ms);
	fflush(NULL);
	_exit(2); // not exit(): we are already inside quick_exit, which must not re-enter
}

/// The audio capture owns the process's lifetime (host_audio.c finalises the WAV and
/// `quick_exit`s after `DELUGE_HOST_CAPTURE_SECONDS`, default 2.0 s). A script's `quit` must be
/// what ends the run instead, so move the capture's deadline to just past the script's last step
/// and send its WAV to /dev/null -- unless the caller asked for either explicitly, in which case
/// they meant it.
///
/// The deadline is the script's own end plus `HOST_SCRIPT_DEADLINE_MARGIN_S`, not something flat
/// and huge: it is the only in-process backstop on a run whose `quit` is never reached (any change
/// that stops the app polling `deluge_control_poll_event` also stops the pump), and a backstop an
/// hour away is no backstop at all. `deadline_hook` turns hitting it into an exit 2 with a reason.
/// A run that stops rendering altogether freezes this clock too; the ctest `TIMEOUT` covers that,
/// and is sized so that it never fires first while rendering is still making progress.
static void defer_capture_exit(void) {
	if (getenv("DELUGE_HOST_CAPTURE_SECONDS") == NULL) {
		char seconds[16];
		snprintf(seconds, sizeof(seconds), "%u", script_last_at_ms / 1000u + HOST_SCRIPT_DEADLINE_MARGIN_S);
		setenv("DELUGE_HOST_CAPTURE_SECONDS", seconds, 0);
	}
	if (getenv("DELUGE_HOST_WAV") == NULL) {
		setenv("DELUGE_HOST_WAV", "/dev/null", 0);
	}
	at_quick_exit(deadline_hook);
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

	validate_script();
	apply_leading_board_steps();
	clear_stale_snapshots();
	defer_capture_exit();
	script_loaded = 1;
	fprintf(stderr, "[host-script] running %s -> %s\n", path, snapshot_dir());
}
