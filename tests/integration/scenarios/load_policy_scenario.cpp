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
/// @brief The load policies that need no loader state to pin, run as `GOLDEN_SCENARIO=load_policy`.
///
/// Each prints one `LP-<CHECK> ok=0|1` line, then `LP-RESULT failures=N`:
///
/// - `LP-CANDIDATES`: `AudioFileManager::proposeLoadCandidates` proposes the right paths in the right order. The
///   async loader tries them in exactly this order, so a wrong order opens the wrong file or misses one.
/// - `LP-ADMISSION`: `AudioFileManager::admissibleAsyncLoadPath` agrees with `path_admission_corpus.inc`, which the
///   Rust loader is tested against too. A looser C++ rule asks the loader for a path it refuses, forever.
/// - `LP-ABANDON`: the sample build contract (`storage/audio/sample_build.h`). Abandoning a build publishes nothing,
///   `applyGeometry` refuses an invalid raw data format, and 20,000 begin/abandon rounds release every chunk they
///   adopt, so a browser cursor that never lands on a file cannot exhaust the chunk table. Runs before anything
///   else loads, so its eviction count is its own.
/// - `LP-OPEN-PATH`: a Sample's read stream (`SampleStream::open_read_stream`) opens exactly the path it is given. A
///   path with an embedded NUL, or with no room for its terminator, answers `FILE_NOT_FOUND` rather than opening
///   the file a shortened path names.
/// - `LP-DEFAULT layer=holder|manager`: `AudioFileHolder::loadFile` and `AudioFileManager::getAudioFileFromFilename`
///   default to the synchronous backing. Against the real loader, which is always active here, a call with the
///   mode omitted loads at once, and the same call made explicitly `AsyncOrBlocking` first reports
///   `LOAD_IN_PROGRESS`; the contrast is what lets a flipped default fail.
///
/// Gate load-policy:
///     the load policies no loader state can show: AudioFileManager::proposeLoadCandidates proposes the right paths in
///     the right order (LP-CANDIDATES) -- the async loader opens them in exactly that order; the C++ admission rule
///     agrees with path_admission_corpus.inc, which the Rust loader is tested against too (LP-ADMISSION); the sample
///     build contract (storage/audio/sample_build.h), where abandoning a build publishes nothing, applyGeometry refuses
///     an invalid raw data format, and 20,000 begin/abandon rounds release every chunk they adopt, so a browser cursor
///     that never lands cannot exhaust the chunk table (LP-ABANDON); a sample's read stream opens exactly the path it
///     is given, refusing one with an embedded NUL or with no room for its terminator rather than opening the file a
///     shortened path names (LP-OPEN-PATH); and AudioFileHolder::loadFile and getAudioFileFromFilename default to the
///     synchronous backing, which a call with the mode omitted shows by loading at once while the same call made
///     explicitly AsyncOrBlocking first reports LOAD_IN_PROGRESS (LP-DEFAULT); the wavetables each fixture builds
///     (LP-WT-PIN: every band's range, geometry and samples, as a CRC per file under
///     tests/unit/dsp/wavetable_band_records.h, for the synthetic set in
///     tests/golden/render/wavetable_pins and the golden presets' wavetables); the pitch each sample's detection
///     finds (LP-PITCH-PIN, by its bits, against tests/integration/scenarios/pitch_pin_records.h); and a single-band
///     table's load (LP-WT-ZEROBAND, an 8-sample cycle, which must hold one band with a non-empty cycle range so no
///     band index below 0 is selected); each allocation of a wavetable load from a file, failed once (LP-WT-FAIL): a
///     band's samples, the band list, the cycle buffer and the spectrum fail the load with INSUFFICIENT_RAM and leave
///     nothing resident, a missing band-0 FFT table leaves one band holding every cycle, a missing smaller band's table
///     drops that band alone, and a missing pitch window table ends the detection with no pitch.
///
/// Gate load-policy-wavetable-churn:
///     wavetables evicted under memory pressure reload as they were built: sixteen distinct copies of a golden
///     wavetable, loaded and released for eight rounds with the host SDRAM clamped so each round must evict, each
///     reload's bands matching the recorded CRC, with the resource manager's eviction count rising, every failing
///     allocation of LP-WT-FAIL repeated each round so a leaked band or chunk exhausts memory, and no reason leaked at
///     the end (LP-INVARIANTS)

#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"

#include "memory/general_memory_allocator.h" // GeneralMemoryAllocator::resourceManager()
#include "memory/heaps.h"                    // failNextAllocationOfSize, allocationFailurePending
#include "model/sample/pitch_scan.h"
#include "model/sample/sample.h"
#include "processing/fft/fft_config_manager.h"
#include "scenarios/load_invariants.h"
#include "scenarios/pitch_pin_records.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/sample_build.h"
#include "storage/wave_table/wave_table.h"
#include "storage/wave_table/wave_table_holder.h"
#include "support/golden_update.h"
#include "unit/dsp/wavetable_band_crc.h"
#include "unit/dsp/wavetable_band_records.h"

#include "deluge_storage_resource.h" // DelugeResourceStats, deluge_resource_stats(_reset)
#include "libdeluge/channel.h"       // DELUGE_PATH_MAX

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// The app headers above can reach `third_party/printf/printf.h`, which aliases `printf` to an embedded printf this
// harness does not link; the lines this file prints are the gate's output, so they go to the real stdout.
#ifdef printf
#undef printf
#endif

DELUGE_SCENARIO_DECLARE(load_policy);

using deluge::storage::AsyncLoadMode;
using deluge::storage::AudioLoadContext;

namespace {

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;

ScenarioReport g_report{"LP"};

namespace golden = deluge::test::golden;
constexpr std::string_view kWaveTableHeader = "tests/unit/dsp/wavetable_band_records.h";
constexpr std::string_view kPitchHeader = "tests/integration/scenarios/pitch_pin_records.h";

/// Pin proposeLoadCandidates()'s policy: which paths a load tries, in what order. The async loader opens candidates
/// in exactly this order, so a wrong order silently opens the wrong file or misses one the right order would find.
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
			g_report.fail("[proposeLoadCandidates]: expected 3 candidates for a single-level SAMPLES/ path, got %zu",
			              count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/kick.wav") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[0] \"SAMPLES/ALT/kick.wav\", got \"%s\"",
				              c[0].c_str());
				ok = false;
			}
			if (c[1] != "SAMPLES/ALT/kick.wav") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[1] \"SAMPLES/ALT/kick.wav\", got \"%s\"",
				              c[1].c_str());
				ok = false;
			}
			if (c[2] != "SAMPLES/kick.wav") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[2] \"SAMPLES/kick.wav\", got \"%s\"",
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
			g_report.fail("[proposeLoadCandidates]: expected 3 candidates for a nested SAMPLES/ path, got %zu", count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/FOLDER_kick.wav") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[0] (flattened) "
				              "\"SAMPLES/ALT/FOLDER_kick.wav\", got \"%s\"",
				              c[0].c_str());
				ok = false;
			}
			if (c[1] != "SAMPLES/ALT/kick.wav") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[1] (bare) \"SAMPLES/ALT/kick.wav\", got "
				              "\"%s\"",
				              c[1].c_str());
				ok = false;
			}
			if (c[2] != "SAMPLES/FOLDER/kick.wav") {
				g_report.fail(
				    "[proposeLoadCandidates]: expected candidate[2] (regular) \"SAMPLES/FOLDER/kick.wav\", got "
				    "\"%s\"",
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
			g_report.fail("[proposeLoadCandidates]: expected 2 candidates for a non-SAMPLES/ path, got %zu", count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/kick.xml") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[0] \"SAMPLES/ALT/kick.xml\", got \"%s\"",
				              c[0].c_str());
				ok = false;
			}
			if (c[1] != "PRESETS/SYNTHS/kick.xml") {
				g_report.fail("[proposeLoadCandidates]: expected candidate[1] \"PRESETS/SYNTHS/kick.xml\", got "
				              "\"%s\"",
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
			g_report.fail("[proposeLoadCandidates]: expected 1 candidate with no alternate dir known, got %zu", count);
			ok = false;
		}
		else if (c[0] != "SAMPLES/kick.wav") {
			g_report.fail("[proposeLoadCandidates]: expected candidate[0] \"SAMPLES/kick.wav\", got \"%s\"",
			              c[0].c_str());
			ok = false;
		}
	}

	return ok;
}

/// Pin the sample build's contracts (storage/audio/sample_build.h): abandoning a build must not publish it, and must
/// not leave it sitting in the manager half-built; applyGeometry() must refuse an invalid raw_data_format rather than
/// silently accept it.
bool checkSampleBuildAbandon() {
	namespace sample_build = deluge::storage::sample_build;
	bool ok = true;

	// abandons a build without publishing it
	{
		const size_t before = audioFileManager.audioFiles.size();
		Error error = Error::NONE;
		sample_build::SampleBuild* build = sample_build::begin("TEST/abandon.wav", "", 4096, &error);
		if (build == nullptr) {
			g_report.fail("[sampleBuildAbandon]: begin() returned null (error=%d)", static_cast<int>(error));
			return false;
		}
		sample_build::abandon(build);
		// Nothing published. NOTE: begin() never inserts into audioFiles (only finish() does), so this
		// alone does not prove abandon() actually released the adopted chunk -- see the loop below for
		// that.
		if (audioFileManager.audioFiles.size() != before) {
			g_report.fail("[sampleBuildAbandon]: audioFiles.size() changed across abandon (%zu -> %zu)", before,
			              audioFileManager.audioFiles.size());
			ok = false;
		}
	}

	// applyGeometry() refuses an invalid raw_data_format (rather than silently degrading it, which
	// would publish a sample that renders wrong with no error) and accepts a valid one.
	{
		Error error = Error::NONE;
		sample_build::SampleBuild* build = sample_build::begin("TEST/geometry.wav", "", 4096, &error);
		if (build == nullptr) {
			g_report.fail("[sampleBuildAbandon]: begin() (geometry case) returned null (error=%d)",
			              static_cast<int>(error));
			return false;
		}

		sample_build::Geometry geometry{};
		geometry.raw_data_format = 200; // not one of RawDataFormat's 0..5 values
		Error geometryError = Error::NONE;
		const bool acceptedInvalid = sample_build::applyGeometry(build, geometry, &geometryError);
		if (acceptedInvalid || geometryError != Error::BUG) {
			g_report.fail("[sampleBuildAbandon]: applyGeometry() accepted an invalid raw_data_format=200 "
			              "(accepted=%d error=%d)",
			              acceptedInvalid, static_cast<int>(geometryError));
			ok = false;
		}

		geometry.raw_data_format = 0; // RawDataFormat::NATIVE -- valid
		geometryError = Error::UNSPECIFIED;
		const bool acceptedValid = sample_build::applyGeometry(build, geometry, &geometryError);
		if (!acceptedValid || geometryError != Error::NONE) {
			g_report.fail("[sampleBuildAbandon]: applyGeometry() refused a valid raw_data_format=0 "
			              "(accepted=%d error=%d)",
			              acceptedValid, static_cast<int>(geometryError));
			ok = false;
		}

		sample_build::abandon(build);
	}

	// abandon() must actually release the adopted resource-manager chunk, not just free the
	// SampleBuild itself. Two complementary checks catch two different leak SHAPES a buggy abandon()
	// could take -- neither alone is sufficient:
	//
	//  (a) A leak that keeps the chunk LEASED (e.g. abandon() forgets to release the reason at all):
	//      the chunk is never evictable, so churn accumulates them forever and the table genuinely
	//      fills. Caught by the crash loop below -- the next begin()'s adopt eventually hits
	//      AudioFileManager::adoptAudioFileObject's FREEZE_WITH_ERROR("RAF1"): a hard, unmistakable
	//      stop rather than a silent pass.
	//  (b) A leak that drops the lease but never actually destroys the object (e.g. abandon() calls removeReason() but
	//      not destroyAudioFileObject()): the chunk sits adopted but UNLEASED, so it stays evictable, and
	//      deluge_resource_adopt's own table-full path silently reclaims it via evict_lowest()
	//      (`src/storage/resource/src/manager.rs`) when room is needed. The crash loop above would then never fail --
	//      begin() just keeps succeeding as old leaked chunks get quietly evicted to make room -- so it is blind to
	//      this shape. evict_lowest() DOES bump `DelugeResourceStats. evictions`
	//      (`src/storage/resource/src/manager.rs`) even though the deliberate evict_chunk() a correct abandon() goes
	//      through does not -- so a correct run should see zero evictions here (nothing in this test creates real
	//      memory pressure), and a stats reset immediately before the loop plus an `evictions == 0` assertion
	//      immediately after catches shape (b) without needing a live chunk-count query the public C ABI doesn't
	//      expose.
	{
		DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
		if (mgr == nullptr) {
			g_report.fail("[sampleBuildAbandon]: no resource manager -- boot did not create it");
			return false;
		}
		deluge_resource_stats_reset(mgr);

		constexpr int kIterations = 20000;
		for (int i = 0; i < kIterations; i++) {
			Error error = Error::NONE;
			sample_build::SampleBuild* build = sample_build::begin("TEST/abandon_loop.wav", "", 4096, &error);
			if (build == nullptr) {
				g_report.fail("[sampleBuildAbandon]: begin/abandon churn ran out of chunk-table slots -- "
				              "begin() failed after %d iterations (error=%d); abandon() is likely leaking",
				              i, static_cast<int>(error));
				ok = false;
				break;
			}
			sample_build::abandon(build);
		}

		DelugeResourceStats stats{};
		deluge_resource_stats(mgr, &stats);
		if (stats.evictions != 0) {
			g_report.fail("[sampleBuildAbandon]: %llu pressure-driven eviction(s) during begin/abandon "
			              "churn that should never have needed any -- abandon() is likely dropping the lease "
			              "without destroying the object (shape (b) above)",
			              static_cast<unsigned long long>(stats.evictions));
			ok = false;
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
			g_report.fail("[pathAdmission]: %s (%zu bytes) should be %s, was %s", c.label, c.bytes.size(),
			              c.accepted ? "accepted" : "refused", accepted ? "accepted" : "refused");
			ok = false;
		}
	}
	return ok;
}

/// The sample every `LP-DEFAULT` load asks for; the card holds it (load_policy.card).
constexpr const char* kDefaultPath = "SAMPLES/DEFAULT.WAV";

/// Pin that a Sample's read stream opens exactly the path it is given: a path the ABI cannot carry whole is refused
/// with the open's own failure, never cut short to a different file. `SAMPLES/DEFAULT.WAV` followed by a NUL and more
/// names no file, though the card holds the file its first 19 bytes name; a path of `DELUGE_PATH_MAX` bytes has no
/// room for its terminator.
bool checkReadStreamPath() {
	namespace sample_build = deluge::storage::sample_build;
	bool ok = true;
	Error error = Error::NONE;
	sample_build::SampleBuild* build = sample_build::begin("TEST/open_path.wav", "", 4096, &error);
	if (build == nullptr) {
		g_report.fail("[readStreamPath]: begin() returned null (error=%d)", static_cast<int>(error));
		return false;
	}
	constexpr std::string_view kEmbeddedNul{"SAMPLES/DEFAULT.WAV\0.bak", 24};
	const Error embedded = sample_build::bindReadStream(build, kEmbeddedNul);
	if (embedded != Error::FILE_NOT_FOUND) {
		g_report.fail(
		    "[readStreamPath]: a path with an embedded NUL answered error=%d, not FILE_NOT_FOUND -- it opened "
		    "the file named by the bytes before the NUL",
		    static_cast<int>(embedded));
		ok = false;
	}
	const std::string overLong(DELUGE_PATH_MAX, 'A');
	const Error tooLong = sample_build::bindReadStream(build, overLong);
	if (tooLong != Error::FILE_NOT_FOUND) {
		g_report.fail("[readStreamPath]: a path of DELUGE_PATH_MAX bytes answered error=%d, not FILE_NOT_FOUND",
		              static_cast<int>(tooLong));
		ok = false;
	}
	sample_build::abandon(build);
	return ok;
}
/// How many times an explicit async load may be answered `LOAD_IN_PROGRESS` before it counts as wedged. A retry bound
/// inside one load's resolution, which its own LP-DEFAULT line and FAIL lines judge, not a wait across steps.
constexpr int kMaxDeferrals = 2000;

enum class Stage : uint8_t {
	Pure,
	WaveTableFail,
	HolderDefault,
	HolderAsync,
	ManagerDefault,
	ManagerAsync,
	WaveTablePin,
	PitchPin,
	ZeroBand,
	WaveTableChurn,
	Finish,
	Done
};

/// Each stage's name in LOAD_POLICY_ONLY.
const char* stageName(Stage stage) {
	switch (stage) {
	case Stage::Pure:
		return "pure";
	case Stage::WaveTableFail:
		return "wt-fail";
	case Stage::HolderDefault:
		return "holder-default";
	case Stage::HolderAsync:
		return "holder-async";
	case Stage::ManagerDefault:
		return "manager-default";
	case Stage::ManagerAsync:
		return "manager-async";
	case Stage::WaveTablePin:
		return "wt-pin";
	case Stage::PitchPin:
		return "pitch-pin";
	case Stage::ZeroBand:
		return "zero-band";
	case Stage::WaveTableChurn:
		return "wt-churn";
	case Stage::Finish:
	case Stage::Done:
		break;
	}
	return "";
}

/// Whether @p stage runs: every gate stage when LOAD_POLICY_ONLY is unset or empty, else only the stages it names
/// (comma-separated). The churn is no gate stage: it runs only when named.
bool stageRuns(Stage stage) {
	const char* only = std::getenv("LOAD_POLICY_ONLY");
	if (only == nullptr || *only == '\0') {
		return stage != Stage::WaveTableChurn;
	}
	const std::string_view name = stageName(stage);
	std::string_view list{only};
	while (true) {
		const std::size_t comma = list.find(',');
		if (list.substr(0, comma) == name) {
			return true;
		}
		if (comma == std::string_view::npos) {
			return false;
		}
		list.remove_prefix(comma + 1);
	}
}

Stage g_stage = Stage::Pure;
bool g_cardStepPending = false;
bool g_summaryPrinted = false;
/// The explicit async load in flight: how many times it has been asked, and whether its first answer was
/// `LOAD_IN_PROGRESS`.
int g_asyncAttempts = 0;
bool g_asyncDeferredFirst = false;
AudioFileHolder g_holder;

/// Prints a check's evidence line. The failures behind `ok=0` are counted where their FAIL lines print.
void report(const char* line, bool ok) {
	printf("%s ok=%d\n", line, ok ? 1 : 0);
}

/// The mode omitted, as every existing call site omits it: the load must happen at once, synchronously.
void runHolderDefault() {
	g_holder = AudioFileHolder{};
	g_holder.filePath = kDefaultPath;
	g_holder.audioFileType = AudioFileType::SAMPLE;
	const Error error = g_holder.loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
	const bool ok = error != Error::LOAD_IN_PROGRESS && g_holder.audioFile != nullptr;
	if (!ok) {
		g_report.fail("[loadFileDefault]: AudioFileHolder::loadFile with the mode omitted did not load at once "
		              "(error=%d audioFile=%p)",
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
		g_report.fail("[loadFileDefault]: the explicit AsyncOrBlocking load (%s) did not reach the async loader: its "
		              "first answer was not LOAD_IN_PROGRESS",
		              layer);
	}
	if (file == nullptr) {
		g_report.fail("[loadFileDefault]: the explicit AsyncOrBlocking load (%s) did not load after %d attempts "
		              "(error=%d)",
		              layer, g_asyncAttempts, static_cast<int>(error));
	}
	if (holderLayer && g_holder.audioFile != nullptr) {
		g_holder.audioFile->removeReason("E391");
		g_holder.audioFile = nullptr;
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kDefaultPath);
	printf("LP-DEFAULT layer=%s mode=async attempts=%d ok=%d\n", layer, g_asyncAttempts, ok ? 1 : 0);
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
		g_report.fail("[loadFileDefault]: getAudioFileFromFilename with the mode omitted did not load at once "
		              "(error=%d file=%p)",
		              static_cast<int>(error), static_cast<void*>(file));
	}
	// Returned held by no reason: nothing to release before deleting.
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(kDefaultPath);
	report("LP-DEFAULT layer=manager mode=omitted", ok);
}

/// The card path of a record's file: wavetable_pins/X under SAMPLES/WTP, presets/… under SAMPLES/WTG.
std::string cardPathOf(const char* recordFile) {
	const std::string_view file{recordFile};
	const std::string_view name = file.substr(file.rfind('/') + 1);
	return std::string{file.starts_with("wavetable_pins/") ? "SAMPLES/WTP/" : "SAMPLES/WTG/"} + std::string{name};
}

/// Builds every pinned wavetable from the card and compares the bands with its record (LP-WT-PIN). Each is loaded the
/// way the browser's single-cycle path loads one (makeWaveTableWorkAtAllCosts), held by no reason, then deleted.
void runWaveTablePins() {
	for (const wavetable_band_records::Record& record : wavetable_band_records::kRecords) {
		std::string path = cardPathOf(record.file);
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::WAVETABLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/true);
		auto* table =
		    (file != nullptr && file->type == AudioFileType::WAVETABLE) ? static_cast<WaveTable*>(file) : nullptr;
		const deluge::dsp::fixed::WaveTableData view =
		    table != nullptr ? table->view() : deluge::dsp::fixed::WaveTableData{};
		const uint32_t crc = table != nullptr ? wavetable_band_crc(view) : 0;
		const bool updating = golden::updating();
		if (updating && table != nullptr) {
			char line[160];
			std::snprintf(line, sizeof line, "    {\"%s\", %d, 0x%08XU},", record.file,
			              static_cast<int>(view.num_bands()), static_cast<unsigned>(crc));
			golden::record(kWaveTableHeader, "kRecords", line);
		}
		// Re-recording, the bands are what the run records; the table must still load.
		const bool ok = table != nullptr && (updating || (view.num_bands() == record.num_bands && crc == record.crc));
		printf("LP-WT-PIN file=%s error=%d bands=%d crc=%08X ok=%d\n", record.file, static_cast<int>(error),
		       static_cast<int>(view.num_bands()), static_cast<unsigned>(crc), ok ? 1 : 0);
		if (!ok) {
			g_report.count();
		}
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	}
}

/// The pitch leg's progress: the record it is on, the sample it loaded and the scan over it, ticked once per step.
std::size_t g_pitchIndex = 0;
Sample* g_pitchSample = nullptr;
deluge::sample::PitchScan g_pitchScan;
constexpr int32_t kMaxPitchSteps = 2000;
Wait g_pitchWait{g_report, "the pitch detection finishing", kMaxPitchSteps};

/// Loads each SAMPLES/PIT sample, runs its pitch detection to Done and compares the result's bits with its record
/// (LP-PITCH-PIN). One step per call; a scan answers Pending while its windows stream in.
/// @return Whether every record has been checked.
bool runPitchPinStep() {
	const auto& records = pitch_pin_records::kRecords;
	if (g_pitchIndex >= records.size()) {
		return true;
	}
	const pitch_pin_records::Record& record = records[g_pitchIndex];
	std::string path = std::string{"SAMPLES/PIT/"} + record.file;
	if (g_pitchSample == nullptr) {
		Error error = Error::NONE;
		AudioFile* file = audioFileManager.getAudioFileFromFilename(path, true, &error, AudioFileType::SAMPLE);
		if (file == nullptr || file->type != AudioFileType::SAMPLE) {
			printf("LP-PITCH-PIN file=%s error=%d ok=0\n", record.file, static_cast<int>(error));
			g_report.count();
			g_pitchIndex++;
			return g_pitchIndex >= records.size();
		}
		g_pitchSample = static_cast<Sample*>(file);
		g_pitchScan.arm(*g_pitchSample, deluge::sample::PitchScan::Limits{});
		g_pitchWait = Wait{g_report, "the pitch detection finishing", kMaxPitchSteps};
	}
	const bool done = g_pitchScan.tick() == deluge::sample::PitchScan::Progress::Done;
	const WaitState waited = g_pitchWait.poll(done);
	if (waited == WaitState::Pending) {
		return false;
	}
	const bool timeout = g_pitchWait.expired();
	const float hz = g_pitchScan.result();
	const uint32_t bits = std::bit_cast<uint32_t>(hz);
	const bool updating = golden::updating();
	if (updating && done) {
		char line[96];
		std::snprintf(line, sizeof line, "    {\"%s\", 0x%08XU},", record.file, static_cast<unsigned>(bits));
		golden::record(kPitchHeader, "kRecords", line);
	}
	// Re-recording, the pitch is what the run records; the scan must still finish.
	const bool ok = done && (updating || bits == record.hz_bits);
	if (timeout) {
		printf("LP-PITCH-PIN file=%s timeout=1 ok=0\n", record.file);
	}
	else {
		printf("LP-PITCH-PIN file=%s hz=%.6f bits=%08X ok=%d\n", record.file, static_cast<double>(hz),
		       static_cast<unsigned>(bits), ok ? 1 : 0);
	}
	// A scan that never finished is counted by its wait.
	if (!ok && !timeout) {
		g_report.count();
	}
	g_pitchScan.cancel();
	g_pitchSample = nullptr;
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	g_pitchIndex++;
	return g_pitchIndex >= records.size();
}

/// An 8-sample cycle builds one band; the load must keep that band's cycle range, or a renderer selects band -1.
void runZeroBand() {
	std::string path = "SAMPLES/WTP/WTP_S8.WAV";
	Error error = Error::NONE;
	AudioFile* file = audioFileManager.getAudioFileFromFilename(path, true, &error, AudioFileType::WAVETABLE, true);
	auto* table = (file != nullptr && file->type == AudioFileType::WAVETABLE) ? static_cast<WaveTable*>(file) : nullptr;
	const deluge::dsp::fixed::WaveTableData view =
	    table != nullptr ? table->view() : deluge::dsp::fixed::WaveTableData{};
	bool ok = table != nullptr && view.num_bands() >= 1;
	for (uint32_t increment : {0U, 0x00100000U, 0x7FFFFFFFU, 0xFFFFFFFFU}) {
		ok = ok && view.select_band(increment) >= 0;
	}
	for (int32_t b = 0; ok && b < view.num_bands(); ++b) {
		ok = view.band(b).fromCycleNumber < view.band(b).toCycleNumber;
	}
	printf("LP-WT-ZEROBAND error=%d bands=%d ok=%d\n", static_cast<int>(error), static_cast<int>(view.num_bands()),
	       ok ? 1 : 0);
	if (!ok) {
		g_report.count();
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
}

/// The record of @p file.
const wavetable_band_records::Record& recordOf(std::string_view file) {
	for (const wavetable_band_records::Record& record : wavetable_band_records::kRecords) {
		if (file == record.file) {
			return record;
		}
	}
	std::abort();
}

/// A wavetable load from the card, as the browser's single-cycle path makes one.
struct WaveTableLoad {
	Error error = Error::NONE;
	AudioFile* file = nullptr;
	WaveTable* table = nullptr;
};

WaveTableLoad loadWaveTable(std::string path) {
	WaveTableLoad load;
	load.file = audioFileManager.getAudioFileFromFilename(
	    path, /*mayReadCard=*/true, &load.error, AudioFileType::WAVETABLE, /*makeWaveTableWorkAtAllCosts=*/true);
	if (load.file != nullptr && load.file->type == AudioFileType::WAVETABLE) {
		load.table = static_cast<WaveTable*>(load.file);
	}
	return load;
}

/// Whether @p path is resident as a wavetable: an in-memory lookup, which reads nothing from the card.
bool residentWaveTable(std::string path) {
	Error error = Error::NONE;
	return audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/false, &error, AudioFileType::WAVETABLE)
	       != nullptr;
}

/// The CRC of @p view's bands but those of @p skippedMagnitude.
uint32_t crcWithout(const deluge::dsp::fixed::WaveTableData& view, int32_t skippedMagnitude) {
	std::vector<deluge::dsp::fixed::WaveTableBand> kept;
	for (int32_t b = 0; b < view.num_bands(); ++b) {
		if (view.band(b).cycleSizeMagnitude != skippedMagnitude) {
			kept.push_back(view.band(b));
		}
	}
	return wavetable_band_crc({view.num_cycles(), view.num_cycle_transitions_magnitude(), view.wave_index_multiplier(),
	                           kept.data(), sizeof(deluge::dsp::fixed::WaveTableBand),
	                           static_cast<int32_t>(kept.size())});
}

/// Reloads @p recordFile without an injected failure and checks its bands against the record: a failed or degraded
/// load left nothing behind that the next load picks up.
bool reloadsAsRecorded(const char* recordFile) {
	const std::string path = cardPathOf(recordFile);
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	const WaveTableLoad load = loadWaveTable(path);
	const wavetable_band_records::Record& record = recordOf(recordFile);
	const bool ok = load.table != nullptr && load.table->view().num_bands() == record.num_bands
	                && wavetable_band_crc(load.table->view()) == record.crc;
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	return ok;
}

void reportFail(const char* site, Error error, int32_t bands, bool ok, const char* suffix = "") {
	const bool injected = !deluge::memory::allocationFailurePending();
	printf("LP-WT-FAIL site=%s injected=%d error=%d bands=%d ok=%d%s\n", site, injected ? 1 : 0,
	       static_cast<int>(error), static_cast<int>(bands), (ok && injected) ? 1 : 0, suffix);
	if (!(ok && injected)) {
		g_report.count();
	}
}

constexpr const char* kManyFile = "wavetable_pins/WTP_MANY.WAV";
constexpr const char* kS32File = "wavetable_pins/WTP_S32.WAV";
/// WTP_MANY's shape: 64 cycles of 256 samples, bands from 256 samples (magnitude 8) down to 8.
constexpr int32_t kManyCycles = 64;
constexpr int32_t kManyCycleSize = 256;

/// A missing band-0 FFT table: the table keeps band 0 alone, over every cycle, and loads.
void runBand0TwiddlesLeg() {
	deluge::memory::failNextAllocationOfSize(FFTConfigManager::blockBytes(5));
	const std::string path = cardPathOf(kS32File);
	const WaveTableLoad load = loadWaveTable(path);
	const deluge::dsp::fixed::WaveTableData view =
	    load.table != nullptr ? load.table->view() : deluge::dsp::fixed::WaveTableData{};
	bool ok = load.table != nullptr && load.error == Error::NONE && view.num_bands() == 1
	          && view.band(0).fromCycleNumber == 0 && view.band(0).toCycleNumber == view.num_cycles();
	for (uint32_t increment : {0U, 0x00100000U, 0x7FFFFFFFU, 0xFFFFFFFFU}) {
		ok = ok && view.select_band(increment) >= 0;
	}
	reportFail("band0-twiddles", load.error, view.num_bands(), ok && reloadsAsRecorded(kS32File));
}

/// A missing smaller band's FFT table: that band alone is dropped, and the rest are built as they always are.
void runBandTwiddlesLeg() {
	constexpr int32_t kMissingMagnitude = 7;
	deluge::memory::failNextAllocationOfSize(FFTConfigManager::blockBytes(kMissingMagnitude));
	const std::string path = cardPathOf(kManyFile);
	const WaveTableLoad load = loadWaveTable(path);
	const deluge::dsp::fixed::WaveTableData view =
	    load.table != nullptr ? load.table->view() : deluge::dsp::fixed::WaveTableData{};
	const uint32_t degradedCrc = load.table != nullptr ? wavetable_band_crc(view) : 0;
	bool ok = load.table != nullptr && load.error == Error::NONE;
	for (int32_t b = 0; ok && b < view.num_bands(); ++b) {
		ok = view.band(b).cycleSizeMagnitude != kMissingMagnitude;
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());

	const WaveTableLoad full = loadWaveTable(path);
	ok = ok && full.table != nullptr && view.num_bands() == full.table->view().num_bands() - 1
	     && degradedCrc == crcWithout(full.table->view(), kMissingMagnitude);
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	reportFail("band-twiddles", load.error, view.num_bands(), ok && reloadsAsRecorded(kManyFile));
}

/// An allocation whose failure fails the load: it returns INSUFFICIENT_RAM, no file, and leaves nothing resident.
void runFailedLoadLeg(const char* site, std::size_t bytes) {
	deluge::memory::failNextAllocationOfSize(bytes);
	const std::string path = cardPathOf(kManyFile);
	const WaveTableLoad load = loadWaveTable(path);
	const bool ok = load.error == Error::INSUFFICIENT_RAM && load.file == nullptr && !residentWaveTable(path);
	reportFail(site, load.error, load.table != nullptr ? load.table->view().num_bands() : 0,
	           ok && reloadsAsRecorded(kManyFile));
}

/// Every allocation that fails a wavetable load outright.
void runFailedLoadLegs() {
	constexpr auto bandBytes = [](int32_t k) {
		return static_cast<std::size_t>(
		           kManyCycles * ((kManyCycleSize >> k) + deluge::dsp::fixed::kWavetableDuplicateSamplesAtCycleEnd))
		       * sizeof(int16_t);
	};
	runFailedLoadLeg("band-samples-0", bandBytes(0));
	runFailedLoadLeg("band-samples-2", bandBytes(2));
	runFailedLoadLeg("bands-growth", 6 * sizeof(WaveTableBand));
	runFailedLoadLeg("cycle-buffer", kManyCycleSize * sizeof(int32_t));
	runFailedLoadLeg("spectrum", ((kManyCycleSize >> 1) + 1) * sizeof(deluge::dsp::fixed::fft::Complex));
}

/// The pitch leg's sample, loaded on its first step; the scan over it is the pitch pins' scan, which is idle here.
Sample* g_failPitchSample = nullptr;
Wait g_failPitchWait{g_report, "the failing pitch load ending", kMaxPitchSteps};

/// A missing pitch window FFT table, made before any detection has built it: the scan ends with no pitch. One step per
/// call, as the pitch pins tick theirs.
/// @return Whether the leg has ended.
bool runPitchTwiddlesStep() {
	std::string path = "SAMPLES/PIT/SINE440.WAV";
	if (g_failPitchSample == nullptr) {
		Error error = Error::NONE;
		AudioFile* file = audioFileManager.getAudioFileFromFilename(path, true, &error, AudioFileType::SAMPLE);
		if (file == nullptr || file->type != AudioFileType::SAMPLE) {
			reportFail("pitch-twiddles", error, 0, false);
			return true;
		}
		g_failPitchSample = static_cast<Sample*>(file);
		deluge::memory::failNextAllocationOfSize(FFTConfigManager::blockBytes(kPitchDetectWindowSizeMagnitude));
		g_pitchScan.arm(*g_failPitchSample, deluge::sample::PitchScan::Limits{});
		g_failPitchWait = Wait{g_report, "the failing pitch load ending", kMaxPitchSteps};
	}
	const bool done = g_pitchScan.tick() == deluge::sample::PitchScan::Progress::Done;
	const WaitState waited = g_failPitchWait.poll(done);
	if (waited == WaitState::Pending) {
		return false;
	}
	if (waited == WaitState::Met) {
		reportFail("pitch-twiddles", Error::NONE, 0, g_pitchScan.result() == 0.0F);
	}
	g_pitchScan.cancel();
	g_failPitchSample = nullptr;
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	return true;
}

/// Each allocation of a wavetable load from a file, failed once (LP-WT-FAIL), after the pitch leg has ended. Each FFT
/// table's failure comes before any load has built that size's table: the tables are kept for the run.
void runWaveTableFail() {
	runBand0TwiddlesLeg();
	runBandTwiddlesLeg();
	runFailedLoadLegs();
}

/// The resource manager's eviction count, over the run. Without a manager (boot did not create it) it reports a
/// failure and returns 0.
uint64_t evictionCount() {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr == nullptr) {
		g_report.fail("[waveTableChurn]: no resource manager -- boot did not create it");
		return 0;
	}
	DelugeResourceStats stats{};
	deluge_resource_stats(mgr, &stats);
	return stats.evictions;
}

constexpr int kChurnRounds = 8;
constexpr int kChurnCopies = 16;
int g_churnRound = 0;
uint64_t g_churnEvictionsAtStart = 0;

/// One round of the churn (LP-WT-CHURN): each SAMPLES/WTC copy of a golden wavetable loaded through a holder, checked
/// against its record and released, then every failing allocation of LP-WT-FAIL again. With the host SDRAM clamped,
/// the copies cannot all stay resident, so a round evicts tables and the next reloads them; a band or chunk that a
/// failed load leaked accumulates over the rounds until a load fails.
/// @return Whether every round has run.
bool runWaveTableChurnStep() {
	const wavetable_band_records::Record& record = recordOf("presets/wavetable/morph_sync.2.wav");
	if (g_churnRound == 0) {
		g_churnEvictionsAtStart = evictionCount();
	}
	const uint64_t evictionsBefore = evictionCount();
	int loads = 0;
	bool crcOk = true;
	for (int copy = 0; copy < kChurnCopies; ++copy) {
		std::array<char, 32> path{};
		snprintf(path.data(), path.size(), "SAMPLES/WTC/WTC%02d.WAV", copy);
		WaveTableHolder holder;
		holder.filePath = path.data();
		holder.audioFileType = AudioFileType::WAVETABLE;
		const Error error =
		    holder.loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
		if (error != Error::NONE || holder.audioFile == nullptr || holder.audioFile->type != AudioFileType::WAVETABLE) {
			continue;
		}
		const deluge::dsp::fixed::WaveTableData view = static_cast<WaveTable*>(holder.audioFile)->view();
		loads++;
		crcOk = crcOk && view.num_bands() == record.num_bands && wavetable_band_crc(view) == record.crc;
	}
	runFailedLoadLegs();
	const bool ok = loads == kChurnCopies && crcOk;
	printf("LP-WT-CHURN round=%d loads=%d evictions=%llu crcOk=%d ok=%d\n", g_churnRound + 1, loads,
	       static_cast<unsigned long long>(evictionCount() - evictionsBefore), crcOk ? 1 : 0, ok ? 1 : 0);
	if (!ok) {
		g_report.count();
	}
	if (++g_churnRound < kChurnRounds) {
		return false;
	}
	const uint64_t evictions = evictionCount() - g_churnEvictionsAtStart;
	printf("LP-WT-CHURN rounds=%d evictions=%llu ok=%d\n", kChurnRounds, static_cast<unsigned long long>(evictions),
	       evictions > 0 ? 1 : 0);
	if (evictions == 0) {
		g_report.count();
	}
	return true;
}

Stage nextStage(Stage stage) {
	return static_cast<Stage>(static_cast<uint8_t>(stage) + 1);
}

void runOneCardStep() {
	while (g_stage < Stage::Finish && !stageRuns(g_stage)) {
		g_stage = nextStage(g_stage);
	}
	switch (g_stage) {
	case Stage::Pure:
		// The abandon churn first: it asserts no pressure-driven eviction, which holds only while nothing else has
		// filled memory.
		report("LP-ABANDON", checkSampleBuildAbandon());
		report("LP-CANDIDATES", checkProposeLoadCandidates());
		report("LP-ADMISSION", checkPathAdmissionCorpus());
		report("LP-OPEN-PATH", checkReadStreamPath());
		g_stage = Stage::WaveTableFail;
		break;
	case Stage::WaveTableFail:
		if (runPitchTwiddlesStep()) {
			runWaveTableFail();
			g_stage = Stage::HolderDefault;
		}
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
			g_stage = Stage::WaveTablePin;
		}
		break;
	case Stage::WaveTablePin:
		runWaveTablePins();
		g_stage = Stage::PitchPin;
		break;
	case Stage::PitchPin:
		if (runPitchPinStep()) {
			g_stage = Stage::ZeroBand;
		}
		break;
	case Stage::ZeroBand:
		runZeroBand();
		g_stage = Stage::WaveTableChurn;
		break;
	case Stage::WaveTableChurn:
		if (runWaveTableChurnStep()) {
			g_stage = Stage::Finish;
		}
		break;
	case Stage::Finish: {
		const bool invariants = checkLoadInvariants("load_policy");
		report("LP-INVARIANTS", invariants);
		if (!invariants) {
			g_report.count();
		}
		g_stage = Stage::Done;
		g_report.printResult();
		g_summaryPrinted = true;
		break;
	}
	case Stage::Done:
		break;
	}
}

} // namespace

void deluge_scenario_load_policy_begin(void) {
	g_stage = Stage::Pure;
	g_report.reset();
	g_cardStepPending = false;
	g_summaryPrinted = false;
	g_asyncAttempts = 0;
	g_asyncDeferredFirst = false;
	g_pitchIndex = 0;
	g_pitchSample = nullptr;
	g_pitchWait = Wait{g_report, "the pitch detection finishing", kMaxPitchSteps};
	g_failPitchSample = nullptr;
	g_failPitchWait = Wait{g_report, "the failing pitch load ending", kMaxPitchSteps};
	g_churnRound = 0;
	// Line-buffered: the harness ends the process without flushing stdio, and these lines are the gate's output.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	printf("LP-BEGIN\n");
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
	return g_report.failures();
}
