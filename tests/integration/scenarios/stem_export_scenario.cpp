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

/// @file
/// @brief The offline stem export of the corpus fixture's song, as a stepped scenario on its own clock.
///
/// Loads the fixture's song (two-phase: the begin dispatches the async directory listing), runs an offline
/// StemExport, and copies the stems out to GOLDEN_STEM_OUT (default `golden_stems`). GOLDEN_FIXTURE picks the song
/// (default cordae) unless GOLDEN_STEM_SONG names one; GOLDEN_STEM_MODE picks the export (CLIP, TRACK, DRUM, MIXDOWN;
/// default TRACK). StemExport::renderWait waits pass by pass for the async fill task to drain the loader queue, so a
/// sample-backed fixture exports real audio.
///
/// Its row is OwnClock: a slot each virtual millisecond, and each condition polled at once and then every 5 ms. The
/// cadence only bounds how often a condition is polled (each phase's poll budget derives from it); the exported audio
/// does not depend on it.
///
/// Every exit 2 prints an `ST-FAIL` line first: a song that is not on the card, a fixture with no song, a refused
/// load, or a phase that did not finish.

#include "harness/level3/host_script.h"
#include "scenarios/streaming_scenario.h"
#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

DELUGE_SCENARIO_DECLARE(stem_export);

namespace {

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;

enum class Phase : uint8_t { Dispatch, Listing, Load, Export, Done };

/// Virtual milliseconds between polls of a condition that does not hold yet.
constexpr int64_t kPollMs = 5;
/// Each phase's bound in polls: its seconds at a poll each 5 ms, plus the poll at the deadline itself.
constexpr int32_t kDispatchPolls = 30'000 / kPollMs + 1;
constexpr int32_t kListingPolls = 30'000 / kPollMs + 1;
constexpr int32_t kLoadPolls = 60'000 / kPollMs + 1;
constexpr int32_t kExportPolls = 300'000 / kPollMs + 1;

ScenarioReport g_report{"ST"};
Phase g_phase = Phase::Done;
Wait g_wait{g_report, "the song load's dispatch", kDispatchPolls};
int64_t g_slot = 0;
int64_t g_nextPoll = 0;
std::string g_songPath;
int32_t g_mode = 1;

[[noreturn]] void wedged(const char* why) {
	std::fprintf(stderr, "golden_vt_render: stem_export: %s\n", why);
	host_script_exit(2);
}

/// The card path of GOLDEN_FIXTURE's song (the fixture tree flattens it to SONGS/<name>.XML), or GOLDEN_STEM_SONG.
std::string songPath() {
	if (const char* song = std::getenv("GOLDEN_STEM_SONG"); song != nullptr) {
		return song;
	}
	const char* env = std::getenv("GOLDEN_FIXTURE");
	const std::string fixture = env != nullptr ? env : "cordae";
	if (fixture == "cordae") {
		return "SONGS/Cordae.XML";
	}
	if (fixture == "highsiderr") {
		return "SONGS/06 Ilove Techno Song_Final_2.XML";
	}
	if (fixture == "icoustic") {
		return "SONGS/Grunnmur 58 Nobass 14 Comm-Test.XML";
	}
	g_report.fail("FIXTURE %s has no known SONGS/*.XML path; set GOLDEN_STEM_SONG", fixture.c_str());
	host_script_exit(2);
}

/// The StemExportType GOLDEN_STEM_MODE names, as the C++ enum's value (CLIP=0, TRACK=1, DRUM=2, MIXDOWN=3 in
/// definitions_cxx.hpp); TRACK by default.
int32_t stemMode() {
	const char* env = std::getenv("GOLDEN_STEM_MODE");
	std::string mode = env != nullptr ? env : "";
	std::ranges::transform(mode, mode.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	if (mode == "CLIP") {
		return 0;
	}
	if (mode == "DRUM") {
		return 2;
	}
	if (mode == "MIXDOWN") {
		return 3;
	}
	return 1;
}

bool conditionHolds() {
	switch (g_phase) {
	case Phase::Dispatch:
		return deluge_scenario_song_load_begin_done();
	case Phase::Listing:
		return !deluge_scenario_song_listing_in_progress();
	case Phase::Load:
		return !deluge_scenario_song_load_in_progress();
	case Phase::Export:
		return deluge_scenario_stem_export_done();
	case Phase::Done:
		break;
	}
	return true;
}

/// The phase after the one whose condition just held, begun.
void advance() {
	switch (g_phase) {
	case Phase::Dispatch:
		if (!deluge_scenario_song_load_begin_ok()) {
			g_report.fail("LOAD %s could not open the load browser", g_songPath.c_str());
			host_script_exit(2);
		}
		g_phase = Phase::Listing;
		g_wait = Wait{g_report, "the song listing", kListingPolls};
		return;
	case Phase::Listing:
		if (!deluge_scenario_commit_song_load()) {
			g_report.fail("LOAD %s is not on the card", g_songPath.c_str());
			host_script_exit(2);
		}
		g_phase = Phase::Load;
		g_wait = Wait{g_report, "the song load", kLoadPolls};
		return;
	case Phase::Load:
		deluge_scenario_start_stem_export(g_mode);
		g_phase = Phase::Export;
		g_wait = Wait{g_report, "the stem export", kExportPolls};
		return;
	case Phase::Export: {
		const char* out = std::getenv("GOLDEN_STEM_OUT");
		deluge_scenario_copy_stems_out(out != nullptr ? out : "golden_stems");
		g_phase = Phase::Done;
		return;
	}
	case Phase::Done:
		return;
	}
}

/// One virtual millisecond. Polls only when the last unmet poll is 5 ms old; a condition that holds moves to the
/// next phase, whose condition is polled in the same instant.
void slot() {
	const int64_t now = g_slot++;
	if (now < g_nextPoll) {
		return;
	}
	while (g_phase != Phase::Done) {
		const WaitState waited = g_wait.poll(conditionHolds());
		if (waited == WaitState::Pending) {
			g_nextPoll = now + kPollMs;
			return;
		}
		if (waited == WaitState::Expired) {
			wedged("a phase did not complete within its wait budget — wedged");
		}
		advance();
	}
}

} // namespace

extern "C" void deluge_scenario_stem_export_begin(void) {
	g_report.reset();
	g_songPath = songPath();
	g_mode = stemMode();
	g_slot = 0;
	g_nextPoll = 0;
	deluge_scenario_start_song_load(g_songPath.c_str());
	g_phase = Phase::Dispatch;
	g_wait = Wait{g_report, "the song load's dispatch", kDispatchPolls};
}

extern "C" bool deluge_scenario_stem_export_complete(void) {
	return g_phase == Phase::Done;
}

extern "C" void deluge_scenario_stem_export_dispatch_step(void) {
	slot();
}

extern "C" bool deluge_scenario_stem_export_step_done(void) {
	slot();
	return true;
}

extern "C" int32_t deluge_scenario_stem_export_failures(void) {
	return g_report.failures();
}
