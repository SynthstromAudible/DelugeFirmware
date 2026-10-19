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

#include "bench/stream_bench.h"

#include "definitions_cxx.hpp"
#include "deluge_resource.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "harness/streaming_underrun.h"
#include "io/debug/log.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/sample_stream.h"
#include "libdeluge/streaming_fill.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "runtime/app_loop.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace deluge::bench {
namespace {

using runtime::appLoop;
using runtime::Micros;
using runtime::nowUs;

constexpr Micros kTick = 50'000;
constexpr Micros kSettle = 3'000'000;
constexpr Micros kIdle = 5'000'000;
constexpr Micros kSteady = 30'000'000;
/// How long the load-while-playing phase runs on once the second song's load is armed or done: the rest of its samples
/// load meanwhile.
constexpr Micros kAfterLoad = 15'000'000;
/// The longest any one stage may wait before the run gives up on it.
constexpr Micros kStageTimeout = 60'000'000;
/// The app-loop jobs reported per phase, slowest first.
constexpr size_t kJobsReported = 6;

enum class Stage : uint8_t {
	Settle,
	OpenFirst,
	LoadingFirst,
	Idle,
	Steady,
	OpenSecond,
	LoadingSecond,
	AfterLoad,
	Report,
	Done,
};

struct Bench {
	Stage stage = Stage::Settle;
	Stage after_report = Stage::Done;
	Micros stage_start = 0;
	Micros phase_start = 0;
	const char* phase = "";
	bool load_done = false;
	bool load_ok = false;
	int32_t failures = 0;
	// The phase being reported, captured when it ended, and the next line of it to print.
	DelugeFillTrace trace{};
	DelugeResourceStats resource{};
	uint64_t underrun_wait = 0;
	uint64_t underrun_unassign = 0;
	Micros phase_us = 0;
	size_t line = 0;
	uint64_t underrun_wait_base = 0;
	uint64_t underrun_unassign_base = 0;
};

Bench g;

void enter(Stage stage) {
	g.stage = stage;
	g.stage_start = nowUs();
}

bool stageTimedOut(const char* what) {
	if (nowUs() - g.stage_start < kStageTimeout) {
		return false;
	}
	D_PRINTLN("STREAM-BENCH-FAIL %s", what);
	g.failures++;
	enter(Stage::Report);
	g.after_report = Stage::Done;
	g.line = 1'000; // nothing of a phase to report
	return true;
}

uint64_t underrunWaits() {
	return deluge_sim_underrun_wait_count();
}
uint64_t underrunUnassigns() {
	return deluge_sim_underrun_unassign_count();
}

/// Starts a measured phase: zeroes every counter it reports.
void beginPhase(const char* phase) {
	g.phase = phase;
	g.phase_start = nowUs();
	deluge_fill_trace_reset();
	if (DelugeResource* mgr = deluge_streaming_resource_manager(); mgr != nullptr) {
		deluge_resource_stats_reset(mgr);
	}
	appLoop.resetStats();
	g.underrun_wait_base = underrunWaits();
	g.underrun_unassign_base = underrunUnassigns();
}

/// Ends the measured phase: captures its counters, then reports them a line per tick and goes on to @p next.
void endPhase(Stage next) {
	g.phase_us = nowUs() - g.phase_start;
	deluge_fill_trace_snapshot(&g.trace);
	g.resource = {};
	if (DelugeResource* mgr = deluge_streaming_resource_manager(); mgr != nullptr) {
		deluge_resource_stats(mgr, &g.resource);
	}
	g.underrun_wait = underrunWaits() - g.underrun_wait_base;
	g.underrun_unassign = underrunUnassigns() - g.underrun_unassign_base;
	g.line = 0;
	g.after_report = next;
	enter(Stage::Report);
}

void printHist(const char* metric, const char* unit, uint64_t n, uint64_t total, uint64_t max, const uint32_t* buckets,
               size_t num_buckets) {
	// Trailing empty buckets are left off, so the line fits the log's buffer.
	size_t used = num_buckets;
	while (used > 0 && buckets[used - 1] == 0) {
		used--;
	}
	std::array<char, 160> list{};
	size_t at = 0;
	for (size_t i = 0; i < used && at < list.size(); i++) {
		int written = snprintf(list.data() + at, list.size() - at, i == 0 ? "%lu" : ",%lu",
		                       static_cast<unsigned long>(buckets[i]));
		if (written < 0) {
			break;
		}
		at += static_cast<size_t>(written);
	}
	D_PRINTLN("STREAM-BENCH-HIST phase=%s metric=%s unit=%s n=%lu mean=%lu max=%lu b=%s", g.phase, metric, unit,
	          static_cast<unsigned long>(n), static_cast<unsigned long>(n != 0 ? total / n : 0),
	          static_cast<unsigned long>(max), list.data());
}

void printTraceHist(const char* metric, const DelugeFillTraceHist& h) {
	printHist(metric, "us", h.n, h.total_us, h.max_us, h.buckets, DELUGE_FILL_TRACE_BUCKETS);
}

/// Prints the phase's slowest app-loop jobs, slowest first.
void printJobs() {
	std::array<runtime::JobStats, runtime::Loop::kMaxJobs> jobs{};
	const size_t count = std::min(appLoop.size(), jobs.size());
	for (size_t id = 0; id < count; id++) {
		jobs[id] = appLoop.stats(static_cast<runtime::JobId>(id));
	}
	std::sort(jobs.begin(), jobs.begin() + count,
	          [](const runtime::JobStats& a, const runtime::JobStats& b) { return a.maxRun > b.maxRun; });
	for (size_t i = 0; i < std::min(count, kJobsReported); i++) {
		D_PRINTLN("STREAM-BENCH-JOB phase=%s name=\"%s\" max_us=%lu runs=%lu", g.phase,
		          jobs[i].name != nullptr ? jobs[i].name : "?", static_cast<unsigned long>(jobs[i].maxRun),
		          static_cast<unsigned long>(jobs[i].runs));
	}
}

/// Prints the next line of the phase being reported; returns false once it is all out.
bool printReportLine() {
	const DelugeFillTrace& t = g.trace;
	const DelugeResourceStats& r = g.resource;
	switch (g.line++) {
	case 0:
		D_PRINTLN("STREAM-BENCH-PHASE phase=%s ms=%lu underrun_wait=%lu underrun_unassign=%lu depth_mean_x100=%lu "
		          "depth_max=%lu",
		          g.phase, static_cast<unsigned long>(g.phase_us / 1000), static_cast<unsigned long>(g.underrun_wait),
		          static_cast<unsigned long>(g.underrun_unassign),
		          static_cast<unsigned long>(
		              r.loader_depth_polls != 0 ? (r.loader_depth_total * 100) / r.loader_depth_polls : 0),
		          static_cast<unsigned long>(r.loader_depth_max));
		return true;
	case 1:
		printHist("queue_wait", "frames", r.queue_wait_samples, r.queue_wait_total, r.queue_wait_max,
		          r.queue_wait_buckets, DELUGE_RESOURCE_LATENCY_BUCKETS);
		return true;
	case 2:
		printHist("fill", "frames", r.fill_samples, r.fill_total, r.fill_max, r.fill_buckets,
		          DELUGE_RESOURCE_LATENCY_BUCKETS);
		return true;
	case 3:
		printTraceHist("wake", t.wake);
		return true;
	case 4:
		printTraceHist("fs_wait", t.fs_wait);
		return true;
	case 5:
		printTraceHist("read", t.read);
		return true;
	case 6:
		printTraceHist("finish", t.finish);
		return true;
	case 7:
		printTraceHist("gap", t.gap);
		return true;
	case 8:
		printTraceHist("pop", t.pop);
		return true;
	case 9:
		printTraceHist("begin", t.begin);
		return true;
	case 10:
		printTraceHist("interaction_fs_wait", t.interaction_fs_wait);
		return true;
	case 11:
		printTraceHist("interaction_fs_hold", t.interaction_fs_hold);
		return true;
	case 12:
		D_PRINTLN("STREAM-BENCH-SCANS phase=%s resident=%lu/%lu ptr=%lu/%lu free=%lu/%lu evict=%lu/%lu", g.phase,
		          static_cast<unsigned long>(r.scan_resident_calls), static_cast<unsigned long>(r.scan_resident_slots),
		          static_cast<unsigned long>(r.scan_ptr_calls), static_cast<unsigned long>(r.scan_ptr_slots),
		          static_cast<unsigned long>(r.scan_free_calls), static_cast<unsigned long>(r.scan_free_slots),
		          static_cast<unsigned long>(r.scan_evict_calls), static_cast<unsigned long>(r.scan_evict_slots));
		return true;
	case 13:
		D_PRINTLN("STREAM-BENCH-FS phase=%s long_waits=%lu,%lu,%lu max_us=%lu,%lu,%lu", g.phase,
		          static_cast<unsigned long>(t.long_fs_waits_by_tier[0]),
		          static_cast<unsigned long>(t.long_fs_waits_by_tier[1]),
		          static_cast<unsigned long>(t.long_fs_waits_by_tier[2]),
		          static_cast<unsigned long>(t.long_fs_wait_max_by_tier[0]),
		          static_cast<unsigned long>(t.long_fs_wait_max_by_tier[1]),
		          static_cast<unsigned long>(t.long_fs_wait_max_by_tier[2]));
		return true;
	case 14:
		printJobs();
		return true;
	default:
		return false;
	}
}

/// The name a song loaded from @p path takes: its file name without the extension.
std::string_view expectedName(std::string_view path) {
	if (size_t slash = path.rfind('/'); slash != std::string_view::npos) {
		path.remove_prefix(slash + 1);
	}
	if (size_t dot = path.rfind('.'); dot != std::string_view::npos) {
		path.remove_suffix(path.size() - dot);
	}
	return path;
}

/// Reports the load that just finished, and fails the run unless it loaded the song at @p path. A browser that cannot
/// find the file loads the nearest one instead, which would measure the wrong song.
void reportLoaded(const char* path) {
	const char* got = currentSong->project.metadata.name.c_str();
	D_PRINTLN("STREAM-BENCH-LOADED song=%s ok=%d", got, g.load_ok ? 1 : 0);
	if (!g.load_ok) {
		g.failures++;
	}
	else if (expectedName(path) != std::string_view{got}) {
		D_PRINTLN("STREAM-BENCH-FAIL asked for %s, the browser loaded %s", path, got);
		g.failures++;
	}
}

void openLoad(const char* path) {
	g.load_done = false;
	g.load_ok = false;
	// Gives the current song a name with the extension, as a completed load never does, so a load that gives up
	// cannot leave a song behind whose name matches.
	currentSong->setSongFullPath(path);
	if (!openUI(&loadSongUI)) {
		D_PRINTLN("STREAM-BENCH-FAIL the load browser would not open for %s", path);
		g.failures++;
	}
}

/// Begins the load once the browser's listing is done; returns whether it has begun. Says what it is waiting on
/// every few seconds.
bool beginLoadWhenListed() {
	if (Browser::isListingInProgress()) {
		static Micros last_said = 0;
		if (nowUs() - last_said >= 5'000'000) {
			last_said = nowUs();
			D_PRINTLN("STREAM-BENCH-WAIT listing load_ui_current=%d listing_failures=%d last_listing_error=%d "
			          "pad_output_space=%lu audio_sample_timer=%lu",
			          getCurrentUI() == &loadSongUI ? 1 : 0, static_cast<int>(Browser::listingFailures()),
			          static_cast<int>(Browser::lastListingFailure()),
			          static_cast<unsigned long>(deluge_control_pad_output_space()),
			          static_cast<unsigned long>(AudioEngine::audioSampleTimer));
		}
		return false;
	}
	loadSongUI.beginLoad([](SongLoadOutcome outcome) {
		g.load_ok = outcome == SongLoadOutcome::Loaded;
		g.load_done = true;
	});
	return true;
}

void step() {
	switch (g.stage) {
	case Stage::Settle:
		if (isCardReady() && !loadSongUI.loadInFlight() && nowUs() >= kSettle) {
			D_PRINTLN("STREAM-BENCH-BEGIN first=\"%s\" second=\"%s\"", DELUGE_STREAM_BENCH_SONG_A,
			          DELUGE_STREAM_BENCH_SONG_B);
			openLoad(DELUGE_STREAM_BENCH_SONG_A);
			enter(Stage::OpenFirst);
		}
		return;
	case Stage::OpenFirst:
		if (beginLoadWhenListed()) {
			enter(Stage::LoadingFirst);
		}
		else {
			stageTimedOut("the first song's listing never finished");
		}
		return;
	case Stage::LoadingFirst:
		if (!g.load_done) {
			stageTimedOut("the first song's load never finished");
			return;
		}
		reportLoaded(DELUGE_STREAM_BENCH_SONG_A);
		beginPhase("idle");
		enter(Stage::Idle);
		return;
	case Stage::Idle:
		if (nowUs() - g.stage_start >= kIdle) {
			endPhase(Stage::Steady);
		}
		return;
	case Stage::Steady:
		if (g.phase != std::string_view{"steady"}) {
			beginPhase("steady");
			playbackHandler.playButtonPressed(kInternalButtonPressLatency);
			g.stage_start = nowUs();
			return;
		}
		if (!playbackHandler.isEitherClockActive()) {
			D_PRINTLN("STREAM-BENCH-FAIL playback stopped during the steady phase");
			g.failures++;
		}
		if (nowUs() - g.stage_start >= kSteady) {
			endPhase(Stage::OpenSecond);
		}
		return;
	case Stage::OpenSecond:
		if (g.phase != std::string_view{"load-while-playing"}) {
			beginPhase("load-while-playing");
			openLoad(DELUGE_STREAM_BENCH_SONG_B);
			g.stage_start = nowUs();
			return;
		}
		if (beginLoadWhenListed()) {
			enter(Stage::LoadingSecond);
		}
		else {
			stageTimedOut("the second song's listing never finished");
		}
		return;
	case Stage::LoadingSecond:
		// While playing, a load that has its essential samples arms a song swap for the end of the current loop and
		// loads the rest meanwhile; it completes only once the swap happens. The arm is the end of the load proper.
		if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
			D_PRINTLN("STREAM-BENCH-ARMED ms=%lu", static_cast<unsigned long>((nowUs() - g.stage_start) / 1000));
			enter(Stage::AfterLoad);
			return;
		}
		if (!g.load_done) {
			stageTimedOut("the second song's load never armed or finished");
			return;
		}
		reportLoaded(DELUGE_STREAM_BENCH_SONG_B);
		enter(Stage::AfterLoad);
		return;
	case Stage::AfterLoad:
		if (nowUs() - g.stage_start >= kAfterLoad) {
			endPhase(Stage::Done);
		}
		return;
	case Stage::Report:
		if (!printReportLine()) {
			if (g.after_report == Stage::Done) {
				if (playbackHandler.isEitherClockActive()) {
					playbackHandler.playButtonPressed(kInternalButtonPressLatency);
				}
				D_PRINTLN("STREAM-BENCH-DONE failures=%d", static_cast<int>(g.failures));
			}
			enter(g.after_report);
		}
		return;
	case Stage::Done:
		return;
	}
}

} // namespace

void noteLoadStep(int phase, uint64_t us, int32_t bound, int32_t empty, int32_t in_flight) {
	// Mirrors LoadSongUI::LoadPhase.
	static constexpr std::array<const char*, 8> kNames{"none", "stage",  "begin",  "essential",
	                                                   "arm",  "finish", "settle", "await_arm"};
	if (us < 50'000) {
		return;
	}
	const char* name = phase >= 0 && static_cast<size_t>(phase) < kNames.size() ? kNames[phase] : "?";
	D_PRINTLN("STREAM-BENCH-LOADSTEP phase=%s ms=%lu bound=%d empty=%d in_flight=%d", name,
	          static_cast<unsigned long>(us / 1000), static_cast<int>(bound), static_cast<int>(empty),
	          static_cast<int>(in_flight));
}

void noteBind(const char* path, uint64_t acquire_us, uint64_t set_us, int instruction) {
	if (acquire_us + set_us < 20'000) {
		return;
	}
	D_PRINTLN("STREAM-BENCH-BIND acquire_ms=%lu set_ms=%lu instruction=%d path=%s",
	          static_cast<unsigned long>(acquire_us / 1000), static_cast<unsigned long>(set_us / 1000), instruction,
	          path);
}

void noteSampleBuild(const char* path, uint64_t begin_us, uint64_t bind_us, uint64_t geometry_us, uint64_t finish_us,
                     uint64_t size_bytes) {
	if (begin_us + bind_us + geometry_us + finish_us < 20'000) {
		return;
	}
	D_PRINTLN("STREAM-BENCH-BUILD begin_ms=%lu bind_ms=%lu geometry_ms=%lu finish_ms=%lu kib=%lu path=%s",
	          static_cast<unsigned long>(begin_us / 1000), static_cast<unsigned long>(bind_us / 1000),
	          static_cast<unsigned long>(geometry_us / 1000), static_cast<unsigned long>(finish_us / 1000),
	          static_cast<unsigned long>(size_bytes / 1024), path);
}

void registerStreamBench() {
	appLoop.add("stream bench", &step, kTick, 0);
}

} // namespace deluge::bench
