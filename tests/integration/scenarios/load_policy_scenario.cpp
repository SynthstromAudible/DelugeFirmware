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

#include "../../dsp/wavetable_band_crc.h"
#include "../../dsp/wavetable_band_records.h"
#include "memory/general_memory_allocator.h" // GeneralMemoryAllocator::resourceManager()
#include "model/sample/pitch_scan.h"
#include "model/sample/sample.h"
#include "scenarios/load_invariants.h"
#include "scenarios/pitch_pin_records.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/sample_build.h"
#include "storage/wave_table/wave_table.h"

#include "deluge_resource.h"   // DelugeResourceStats, deluge_resource_stats(_reset)
#include "libdeluge/channel.h" // DELUGE_PATH_MAX

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
			printf("FAIL [sampleBuildAbandon]: begin() returned null (error=%d)\n", static_cast<int>(error));
			return false;
		}
		sample_build::abandon(build);
		// Nothing published. NOTE: begin() never inserts into audioFiles (only finish() does), so this
		// alone does not prove abandon() actually released the adopted chunk -- see the loop below for
		// that.
		if (audioFileManager.audioFiles.size() != before) {
			printf("FAIL [sampleBuildAbandon]: audioFiles.size() changed across abandon (%zu -> %zu)\n", before,
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
			printf("FAIL [sampleBuildAbandon]: begin() (geometry case) returned null (error=%d)\n",
			       static_cast<int>(error));
			return false;
		}

		sample_build::Geometry geometry{};
		geometry.raw_data_format = 200; // not one of RawDataFormat's 0..5 values
		Error geometryError = Error::NONE;
		const bool acceptedInvalid = sample_build::applyGeometry(build, geometry, &geometryError);
		if (acceptedInvalid || geometryError != Error::BUG) {
			printf("FAIL [sampleBuildAbandon]: applyGeometry() accepted an invalid raw_data_format=200 "
			       "(accepted=%d error=%d)\n",
			       acceptedInvalid, static_cast<int>(geometryError));
			ok = false;
		}

		geometry.raw_data_format = 0; // RawDataFormat::NATIVE -- valid
		geometryError = Error::UNSPECIFIED;
		const bool acceptedValid = sample_build::applyGeometry(build, geometry, &geometryError);
		if (!acceptedValid || geometryError != Error::NONE) {
			printf("FAIL [sampleBuildAbandon]: applyGeometry() refused a valid raw_data_format=0 "
			       "(accepted=%d error=%d)\n",
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
			Error error = Error::NONE;
			sample_build::SampleBuild* build = sample_build::begin("TEST/abandon_loop.wav", "", 4096, &error);
			if (build == nullptr) {
				printf("FAIL [sampleBuildAbandon]: begin/abandon churn ran out of chunk-table slots -- "
				       "begin() failed after %d iterations (error=%d); abandon() is likely leaking\n",
				       i, static_cast<int>(error));
				ok = false;
				break;
			}
			sample_build::abandon(build);
		}

		if (mgr != nullptr) {
			DelugeResourceStats stats{};
			deluge_resource_stats(mgr, &stats);
			if (stats.evictions != 0) {
				printf("FAIL [sampleBuildAbandon]: %llu pressure-driven eviction(s) during begin/abandon "
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
		printf("FAIL [readStreamPath]: begin() returned null (error=%d)\n", static_cast<int>(error));
		return false;
	}
	constexpr std::string_view kEmbeddedNul{"SAMPLES/DEFAULT.WAV\0.bak", 24};
	const Error embedded = sample_build::bindReadStream(build, kEmbeddedNul);
	if (embedded != Error::FILE_NOT_FOUND) {
		printf("FAIL [readStreamPath]: a path with an embedded NUL answered error=%d, not FILE_NOT_FOUND -- it opened "
		       "the file named by the bytes before the NUL\n",
		       static_cast<int>(embedded));
		ok = false;
	}
	const std::string overLong(DELUGE_PATH_MAX, 'A');
	const Error tooLong = sample_build::bindReadStream(build, overLong);
	if (tooLong != Error::FILE_NOT_FOUND) {
		printf("FAIL [readStreamPath]: a path of DELUGE_PATH_MAX bytes answered error=%d, not FILE_NOT_FOUND\n",
		       static_cast<int>(tooLong));
		ok = false;
	}
	sample_build::abandon(build);
	return ok;
}
/// Ticks an explicit async load may stay `LOAD_IN_PROGRESS` before it counts as wedged.
constexpr int kMaxDeferrals = 2000;

enum class Stage : uint8_t {
	Pure,
	HolderDefault,
	HolderAsync,
	ManagerDefault,
	ManagerAsync,
	WaveTablePin,
	PitchPin,
	ZeroBand,
	Done
};

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

bool recording() {
	return std::getenv("LOAD_POLICY_RECORD") != nullptr;
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
		if (recording()) {
			printf("LP-RECORD {\"%s\", %d, 0x%08XU},\n", record.file, static_cast<int>(view.num_bands()),
			       static_cast<unsigned>(crc));
		}
		const bool ok = table != nullptr && view.num_bands() == record.num_bands && crc == record.crc;
		printf("LP-WT-PIN file=%s error=%d bands=%d crc=%08X ok=%d\n", record.file, static_cast<int>(error),
		       static_cast<int>(view.num_bands()), static_cast<unsigned>(crc), ok ? 1 : 0);
		if (!ok) {
			g_failures++;
		}
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
	}
}

/// The pitch leg's progress: the record it is on, the sample it loaded and the scan over it, ticked once per step.
std::size_t g_pitchIndex = 0;
Sample* g_pitchSample = nullptr;
deluge::sample::PitchScan g_pitchScan;
int g_pitchSteps = 0;
constexpr int kMaxPitchSteps = 2000;

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
			g_failures++;
			g_pitchIndex++;
			return g_pitchIndex >= records.size();
		}
		g_pitchSample = static_cast<Sample*>(file);
		g_pitchScan.arm(*g_pitchSample, deluge::sample::PitchScan::Limits{});
		g_pitchSteps = 0;
	}
	const bool done = g_pitchScan.tick() == deluge::sample::PitchScan::Progress::Done;
	const bool timeout = !done && ++g_pitchSteps >= kMaxPitchSteps;
	if (!done && !timeout) {
		return false;
	}
	const float hz = g_pitchScan.result();
	const uint32_t bits = std::bit_cast<uint32_t>(hz);
	if (recording() && done) {
		printf("LP-RECORD {\"%s\", 0x%08XU},\n", record.file, static_cast<unsigned>(bits));
	}
	const bool ok = done && bits == record.hz_bits;
	if (timeout) {
		printf("LP-PITCH-PIN file=%s timeout=1 ok=0\n", record.file);
	}
	else {
		printf("LP-PITCH-PIN file=%s hz=%.6f bits=%08X ok=%d\n", record.file, static_cast<double>(hz),
		       static_cast<unsigned>(bits), ok ? 1 : 0);
	}
	if (!ok) {
		g_failures++;
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
		g_failures++;
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(path.c_str());
}

void runOneCardStep() {
	switch (g_stage) {
	case Stage::Pure:
		// The abandon churn first: it asserts no pressure-driven eviction, which holds only while nothing else has
		// filled memory.
		report("LP-ABANDON", checkSampleBuildAbandon());
		report("LP-CANDIDATES", checkProposeLoadCandidates());
		report("LP-ADMISSION", checkPathAdmissionCorpus());
		report("LP-OPEN-PATH", checkReadStreamPath());
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
		report("LP-INVARIANTS", checkLoadInvariants("load_policy"));
		g_stage = Stage::Done;
		printf("LP-RESULT failures=%d\n", static_cast<int>(g_failures));
		g_summaryPrinted = true;
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
	g_pitchIndex = 0;
	g_pitchSample = nullptr;
	g_pitchSteps = 0;
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
	return g_failures;
}
