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

#include "scenarios/load_policy_scenario.h"

#include "libdeluge/audio_load.h"
#include "memory/general_memory_allocator.h" // GeneralMemoryAllocator::resourceManager()
#include "model/sample/sample.h"
#include "scenarios/load_invariants.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"

#include "deluge_resource.h" // DelugeResourceStats, deluge_resource_stats(_reset)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

// The app headers above can reach `src/lib/printf.h`, which aliases `printf` to an embedded printf this harness does
// not link; the lines this file prints are the gate's output, so they go to the real stdout.
#ifdef printf
#undef printf
#endif

using deluge::storage::AsyncLoadMode;
using deluge::storage::AudioLoadContext;

namespace {

/// Pin proposeLoadCandidates()'s policy: which paths a load tries, in what order. The async loader opens candidates
/// in exactly this order, so a wrong order silently opens the wrong file or misses one it used to find.
bool checkProposeLoadCandidates() {
	bool ok = true;
	using Candidates = std::array<std::string, AudioFileManager::kMaxLoadCandidates>;

	// proposes the alternate directory (bare-name form) before the regular path when it exists
	{
		// Order is behaviour: resolveFileSize tries the alternate dir first whenever it exists, and
		// the async loader opens candidates in the order this function returns them. For a single-level
		// "SAMPLES/..." path the flattened and bare alternate names coincide, so this exercises the
		// 2-distinct-candidate shape (duplicate alt name, then the regular path).
		const AudioLoadContext context{.alternateDir = "SAMPLES/ALT",
		                               .alternateDirStatus = AlternateLoadDirStatus::DOES_EXIST};
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates(context, "SAMPLES/kick.wav", c.data(), c.size());
		if (count != 3) {
			printf("FAIL [proposeLoadCandidates]: expected 3 candidates for a single-level SAMPLES/ path, got %zu\n",
			       count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[0] \"SAMPLES/ALT/kick.wav\", got \"%s\"\n",
				       c[0].c_str());
				ok = false;
			}
			if (c[1] != "SAMPLES/ALT/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[1] \"SAMPLES/ALT/kick.wav\", got \"%s\"\n",
				       c[1].c_str());
				ok = false;
			}
			if (c[2] != "SAMPLES/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[2] \"SAMPLES/kick.wav\", got \"%s\"\n",
				       c[2].c_str());
				ok = false;
			}
		}
	}

	// a nested SAMPLES/ path proposes the flattened alt name, then the bare alt name, then the regular
	// path -- a naive two-candidate policy drops the bare-name-in-alt-dir fallback here, so it's the
	// one that most needs pinning.
	{
		const AudioLoadContext context{.alternateDir = "SAMPLES/ALT",
		                               .alternateDirStatus = AlternateLoadDirStatus::DOES_EXIST};
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates(context, "SAMPLES/FOLDER/kick.wav", c.data(), c.size());
		if (count != 3) {
			printf("FAIL [proposeLoadCandidates]: expected 3 candidates for a nested SAMPLES/ path, got %zu\n", count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/FOLDER_kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[0] (flattened) "
				       "\"SAMPLES/ALT/FOLDER_kick.wav\", got \"%s\"\n",
				       c[0].c_str());
				ok = false;
			}
			if (c[1] != "SAMPLES/ALT/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[1] (bare) \"SAMPLES/ALT/kick.wav\", got "
				       "\"%s\"\n",
				       c[1].c_str());
				ok = false;
			}
			if (c[2] != "SAMPLES/FOLDER/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[2] (regular) \"SAMPLES/FOLDER/kick.wav\", got "
				       "\"%s\"\n",
				       c[2].c_str());
				ok = false;
			}
		}
	}

	// a path outside SAMPLES/ skips the flattened-name candidate: bare alt name, then regular path.
	{
		const AudioLoadContext context{.alternateDir = "SAMPLES/ALT",
		                               .alternateDirStatus = AlternateLoadDirStatus::DOES_EXIST};
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates(context, "PRESETS/SYNTHS/kick.xml", c.data(), c.size());
		if (count != 2) {
			printf("FAIL [proposeLoadCandidates]: expected 2 candidates for a non-SAMPLES/ path, got %zu\n", count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/kick.xml") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[0] \"SAMPLES/ALT/kick.xml\", got \"%s\"\n",
				       c[0].c_str());
				ok = false;
			}
			if (c[1] != "PRESETS/SYNTHS/kick.xml") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[1] \"PRESETS/SYNTHS/kick.xml\", got "
				       "\"%s\"\n",
				       c[1].c_str());
				ok = false;
			}
		}
	}

	// proposes only the regular path when no alternate directory exists
	{
		const AudioLoadContext context{};
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates(context, "SAMPLES/kick.wav", c.data(), c.size());
		if (count != 1) {
			printf("FAIL [proposeLoadCandidates]: expected 1 candidate with no alternate dir known, got %zu\n", count);
			ok = false;
		}
		else if (c[0] != "SAMPLES/kick.wav") {
			printf("FAIL [proposeLoadCandidates]: expected candidate[0] \"SAMPLES/kick.wav\", got \"%s\"\n",
			       c[0].c_str());
			ok = false;
		}
	}

	return ok;
}

/// Pin the C ABI build's contracts (libdeluge/audio_load.h): abandoning a build must not publish it, and must not
/// leave it sitting in the manager half-built; apply_geometry() must refuse an invalid raw_data_format rather than
/// silently accept it.
bool checkAudioLoadAbiAbandon() {
	bool ok = true;

	// abandons a build without publishing it
	{
		const size_t before = audioFileManager.audioFiles.size();
		uint8_t error = 0;
		DelugeSampleBuild* build = deluge_audio_load_begin("TEST/abandon.wav", "", 4096, &error);
		if (build == nullptr) {
			printf("FAIL [audioLoadAbiAbandon]: deluge_audio_load_begin returned NULL (error=%u)\n", error);
			return false;
		}
		deluge_audio_load_abandon(build);
		// Nothing published. NOTE: begin() never inserts into audioFiles (only finish() does), so this
		// alone does not prove abandon() actually released the adopted chunk -- see the loop below for
		// that.
		if (audioFileManager.audioFiles.size() != before) {
			printf("FAIL [audioLoadAbiAbandon]: audioFiles.size() changed across abandon (%zu -> %zu)\n", before,
			       audioFileManager.audioFiles.size());
			ok = false;
		}
	}

	// apply_geometry() refuses an invalid raw_data_format (rather than silently degrading it, which
	// would publish a sample that renders wrong with no error) and accepts a valid one.
	{
		uint8_t error = 0;
		DelugeSampleBuild* build = deluge_audio_load_begin("TEST/geometry.wav", "", 4096, &error);
		if (build == nullptr) {
			printf("FAIL [audioLoadAbiAbandon]: deluge_audio_load_begin (geometry case) returned NULL "
			       "(error=%u)\n",
			       error);
			return false;
		}

		DelugeAudioLoadGeometry geometry{};
		geometry.raw_data_format = 200; // not one of RawDataFormat's 0..5 values
		uint8_t geometryError = 0;
		const bool acceptedInvalid = deluge_audio_load_apply_geometry(build, &geometry, &geometryError);
		if (acceptedInvalid || geometryError != static_cast<uint8_t>(Error::BUG)) {
			printf("FAIL [audioLoadAbiAbandon]: apply_geometry() accepted an invalid raw_data_format=200 "
			       "(accepted=%d error=%u)\n",
			       acceptedInvalid, geometryError);
			ok = false;
		}

		geometry.raw_data_format = 0; // RawDataFormat::NATIVE -- valid
		geometryError = 0;
		const bool acceptedValid = deluge_audio_load_apply_geometry(build, &geometry, &geometryError);
		if (!acceptedValid || geometryError != static_cast<uint8_t>(Error::NONE)) {
			printf("FAIL [audioLoadAbiAbandon]: apply_geometry() refused a valid raw_data_format=0 "
			       "(accepted=%d error=%u)\n",
			       acceptedValid, geometryError);
			ok = false;
		}

		deluge_audio_load_abandon(build);
	}

	// abandon() must actually release the adopted resource-manager chunk, not just free the
	// BuildHandle wrapper. Two complementary checks catch two different leak SHAPES a buggy abandon()
	// could take -- neither alone is sufficient:
	//
	//  (a) A leak that keeps the chunk LEASED (e.g. abandon() forgets to release the reason at all):
	//      the chunk is never evictable, so churn accumulates them forever and the table genuinely
	//      fills. Caught by the crash loop below -- the next begin()'s adopt eventually hits
	//      AudioFileManager::adoptAudioFileObject's FREEZE_WITH_ERROR("RAF1"): a hard, unmistakable
	//      stop rather than a silent pass.
	//  (b) A leak that drops the lease but never actually destroys the object (e.g. abandon() calls
	//      removeReason() but not destroyAudioFileObject()): the chunk sits adopted but UNLEASED, so
	//      it stays evictable, and deluge_resource_adopt's own table-full path silently reclaims it
	//      via evict_lowest() (manager.rs) when room is needed. The crash loop above would then never
	//      fail -- begin() just keeps succeeding as old leaked chunks get quietly evicted to make
	//      room -- so it is blind to this shape. evict_lowest() DOES bump `DelugeResourceStats.
	//      evictions` (manager.rs:1054) even though the deliberate evict_chunk() a correct abandon()
	//      goes through does not (manager.rs:1296-1312) -- so a correct run should see zero evictions
	//      here (nothing in this test creates real memory pressure), and a stats reset immediately
	//      before the loop plus an `evictions == 0` assertion immediately after catches shape (b)
	//      without needing a live chunk-count query the public C ABI doesn't expose.
	{
		DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
		if (mgr != nullptr) {
			deluge_resource_stats_reset(mgr);
		}

		constexpr int kIterations = 20000;
		for (int i = 0; i < kIterations; i++) {
			uint8_t error = 0;
			DelugeSampleBuild* build = deluge_audio_load_begin("TEST/abandon_loop.wav", "", 4096, &error);
			if (build == nullptr) {
				printf("FAIL [audioLoadAbiAbandon]: begin/abandon churn ran out of chunk-table slots -- "
				       "begin() failed after %d iterations (error=%u); abandon() is likely leaking\n",
				       i, error);
				ok = false;
				break;
			}
			deluge_audio_load_abandon(build);
		}

		if (mgr != nullptr) {
			DelugeResourceStats stats{};
			deluge_resource_stats(mgr, &stats);
			if (stats.evictions != 0) {
				printf("FAIL [audioLoadAbiAbandon]: %llu pressure-driven eviction(s) during begin/abandon "
				       "churn that should never have needed any -- abandon() is likely dropping the lease "
				       "without destroying the object (shape (b) above)\n",
				       static_cast<unsigned long long>(stats.evictions));
				ok = false;
			}
		}
	}

	return ok;
}

/// Pin the admission rule against the corpus the Rust side checks itself with. The rule decides which paths ever
/// become an async request; a looser rule here than the loader's is a load asked for forever and never completed.
bool checkPathAdmissionCorpus() {
	struct AdmissionCase {
		bool accepted;
		std::string bytes;
		const char* label;
	};
	const auto repeated = [](std::string_view unit, size_t times) {
		std::string out;
		out.reserve(unit.size() * times);
		for (size_t i = 0; i < times; i++) {
			out.append(unit);
		}
		return out;
	};

	const AdmissionCase cases[] = {
#define DELUGE_PATH_ADMISSION_CASE(accepted, bytes, repeat, label)                                                     \
	{accepted, repeated(std::string_view{bytes, sizeof(bytes) - 1}, repeat), label},
#include "storage/audio/path_admission_corpus.inc"
#undef DELUGE_PATH_ADMISSION_CASE
	};

	bool ok = true;
	for (const AdmissionCase& c : cases) {
		const bool accepted = AudioFileManager::admissibleAsyncLoadPath(c.bytes);
		if (accepted != c.accepted) {
			printf("FAIL [pathAdmission]: %s (%zu bytes) should be %s, was %s\n", c.label, c.bytes.size(),
			       c.accepted ? "accepted" : "refused", accepted ? "accepted" : "refused");
			ok = false;
		}
	}
	return ok;
}

/// The sample every `LP-DEFAULT` load asks for; the card holds it (golden_vt_render's `fill_load_policy_card`).
constexpr const char* kDefaultPath = "SAMPLES/DEFAULT.WAV";
/// Ticks an explicit async load may stay `LOAD_IN_PROGRESS` before it counts as wedged.
constexpr int kMaxDeferrals = 2000;

enum class Stage : uint8_t { Pure, HolderDefault, HolderAsync, ManagerDefault, ManagerAsync, Done };

Stage g_stage = Stage::Pure;
int32_t g_failures = 0;
bool g_cardStepPending = false;
bool g_summaryPrinted = false;
/// The explicit async load in flight: how many times it has been asked, and whether its first answer was
/// `LOAD_IN_PROGRESS`.
int g_asyncAttempts = 0;
bool g_asyncDeferredFirst = false;
AudioFileHolder g_holder;

void report(const char* line, bool ok) {
	printf("%s ok=%d\n", line, ok ? 1 : 0);
	if (!ok) {
		g_failures++;
	}
}

/// The mode omitted, as every existing call site omits it: the load must happen at once, synchronously.
void runHolderDefault() {
	g_holder = AudioFileHolder{};
	g_holder.filePath = kDefaultPath;
	g_holder.audioFileType = AudioFileType::SAMPLE;
	const Error error = g_holder.loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
	const bool ok = error != Error::LOAD_IN_PROGRESS && g_holder.audioFile != nullptr;
	if (!ok) {
		printf("FAIL [loadFileDefault]: AudioFileHolder::loadFile with the mode omitted did not load at once "
		       "(error=%d audioFile=%p)\n",
		       static_cast<int>(error), static_cast<void*>(g_holder.audioFile));
	}
	if (g_holder.audioFile != nullptr) {
		// The reason setAudioFile took, dropped with the tag setAudioFile itself releases with, so the file can be
		// freed: a resident file would answer the contrast below without reaching the loader.
		g_holder.audioFile->removeReason("E391");
		g_holder.audioFile = nullptr;
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kDefaultPath);
	report("LP-DEFAULT layer=holder mode=omitted", ok);
}

/// The same call made explicitly `AsyncOrBlocking`: its first answer must be `LOAD_IN_PROGRESS`, which is what shows
/// the omitted mode above really took the other branch. Asked again each tick until the load resolves, as the
/// contract requires of a caller, so nothing is left in flight.
/// @return whether the load has resolved.
bool runOneAsync(bool holderLayer) {
	Error error = Error::NONE;
	AudioFile* file = nullptr;
	if (holderLayer) {
		if (g_asyncAttempts == 0) {
			g_holder = AudioFileHolder{};
			g_holder.filePath = kDefaultPath;
			g_holder.audioFileType = AudioFileType::SAMPLE;
		}
		error =
		    g_holder.loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true,
		                      CLUSTER_ENQUEUE, /*makeWaveTableWorkAtAllCosts=*/false, AsyncLoadMode::AsyncOrBlocking);
		file = g_holder.audioFile;
	}
	else {
		std::string path = kDefaultPath;
		file = audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                                 /*makeWaveTableWorkAtAllCosts=*/false,
		                                                 AsyncLoadMode::AsyncOrBlocking);
	}
	g_asyncAttempts++;
	if (g_asyncAttempts == 1) {
		g_asyncDeferredFirst = error == Error::LOAD_IN_PROGRESS;
	}
	if (error == Error::LOAD_IN_PROGRESS && g_asyncAttempts <= kMaxDeferrals) {
		return false;
	}

	const char* layer = holderLayer ? "holder" : "manager";
	const bool ok = g_asyncDeferredFirst && file != nullptr;
	if (!g_asyncDeferredFirst) {
		printf("FAIL [loadFileDefault]: the explicit AsyncOrBlocking load (%s) did not reach the async loader: its "
		       "first answer was not LOAD_IN_PROGRESS\n",
		       layer);
	}
	if (file == nullptr) {
		printf("FAIL [loadFileDefault]: the explicit AsyncOrBlocking load (%s) did not load after %d attempts "
		       "(error=%d)\n",
		       layer, g_asyncAttempts, static_cast<int>(error));
	}
	if (holderLayer && g_holder.audioFile != nullptr) {
		g_holder.audioFile->removeReason("E391");
		g_holder.audioFile = nullptr;
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kDefaultPath);
	printf("LP-DEFAULT layer=%s mode=async attempts=%d ok=%d\n", layer, g_asyncAttempts, ok ? 1 : 0);
	if (!ok) {
		g_failures++;
	}
	g_asyncAttempts = 0;
	g_asyncDeferredFirst = false;
	return true;
}

/// `getAudioFileFromFilename` is also called directly with the mode omitted (the folder load), past the holder's
/// default, so its own default is pinned separately.
void runManagerDefault() {
	std::string path = kDefaultPath;
	Error error = Error::NONE;
	AudioFile* file =
	    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE);
	const bool ok = error != Error::LOAD_IN_PROGRESS && file != nullptr;
	if (!ok) {
		printf("FAIL [loadFileDefault]: getAudioFileFromFilename with the mode omitted did not load at once "
		       "(error=%d file=%p)\n",
		       static_cast<int>(error), static_cast<void*>(file));
	}
	// Returned held by no reason: nothing to release before deleting.
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kDefaultPath);
	report("LP-DEFAULT layer=manager mode=omitted", ok);
}

void runOneCardStep() {
	switch (g_stage) {
	case Stage::Pure:
		// The abandon churn first: it asserts no pressure-driven eviction, which holds only while nothing else has
		// filled memory.
		report("LP-ABANDON", checkAudioLoadAbiAbandon());
		report("LP-CANDIDATES", checkProposeLoadCandidates());
		report("LP-ADMISSION", checkPathAdmissionCorpus());
		g_stage = Stage::HolderDefault;
		break;
	case Stage::HolderDefault:
		runHolderDefault();
		g_stage = Stage::HolderAsync;
		break;
	case Stage::HolderAsync:
		if (runOneAsync(/*holderLayer=*/true)) {
			g_stage = Stage::ManagerDefault;
		}
		break;
	case Stage::ManagerDefault:
		runManagerDefault();
		g_stage = Stage::ManagerAsync;
		break;
	case Stage::ManagerAsync:
		if (runOneAsync(/*holderLayer=*/false)) {
			report("LP-INVARIANTS", checkLoadInvariants("load_policy"));
			g_stage = Stage::Done;
			printf("LP-RESULT failures=%d\n", static_cast<int>(g_failures));
			g_summaryPrinted = true;
		}
		break;
	case Stage::Done:
		break;
	}
}

} // namespace

void deluge_scenario_load_policy_begin(void) {
	g_stage = Stage::Pure;
	g_failures = 0;
	g_cardStepPending = false;
	g_summaryPrinted = false;
	g_asyncAttempts = 0;
	g_asyncDeferredFirst = false;
	// Line-buffered: the harness ends the process without flushing stdio, and these lines are the gate's output.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	printf("LP-BEGIN asyncActive=%d\n", deluge_audio_load_async_active() ? 1 : 0);
}

bool deluge_scenario_load_policy_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_load_policy_dispatch_step(void) {
	// The loads open files: card I/O, run when the runner next asks whether the step is done.
	g_cardStepPending = true;
}

bool deluge_scenario_load_policy_step_done(void) {
	if (g_cardStepPending) {
		g_cardStepPending = false;
		runOneCardStep();
	}
	return true;
}

int32_t deluge_scenario_load_policy_failures(void) {
	return g_failures;
}
