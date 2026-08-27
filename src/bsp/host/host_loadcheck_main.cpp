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

/// deluge_loadcheck — headless "load these audio files and print their parsed descriptors" utility.
///
/// Drives the real file-loading path (AudioFileManager::getAudioFileFromFilename → FileByteSource →
/// parseAudioFileHeader → buildSample) against a project directory served through the host efatfs
/// passthrough (host_efatfs_passthrough.cpp, DELUGE_SD_ROOT), so it exercises the construction path
/// end-to-end (directory walk + on-demand cluster streaming + header parse). It is the Tier-2 regression
/// net for the file-loading redesign: load known WAV/AIFF files off the project directory and dump every
/// descriptor field; a diff in the output across a refactor flags a behavioural change.
///
/// Usage: deluge_loadcheck [--project <dir> (--file SAMPLES/A.WAV | --wavetable PATH)...]
/// Deterministic. No external tools required — the project directory IS the SD card.
///
/// With no --file/--wavetable at all, main() returns immediately after running
/// checkProposeLoadCandidates()'s I/O-free policy checks, checkAudioLoadAbiAbandon()'s libdeluge/
/// audio_load.h ownership check, checkAsyncCollector()'s async-load miss path, and
/// checkLoadFileDefaultStaysSynchronous()'s pin that every unmodified loadFile call site stays
/// synchronous -- it does not call
/// deluge_platform_init() or deluge_main(), so there is no app boot, no card fixture, and --project is
/// ignored (each check self-initializes the allocator/resource-manager it touches, and the collector
/// check serves itself a temp directory as its card and removes it again). This is
/// what the harness registry entry for this binary runs as a standing gate (harness/registry.toml);
/// running it with zero arguments from any directory, including the repo root, leaves the tree
/// untouched.
///
/// @warning **Every async-load check in this file drives a STUB** (`g_asyncStub`, below), not the
///          Rust loader. That is not a shortcut, it is a property of this binary: the real loader is
///          a module of the bin-only `deluge-bsp-rust` crate, its `ProdOps` is wired to the Rust
///          efatfs shim (whose `deluge_efatfs_*` symbols this binary already defines itself, in C++,
///          via host_efatfs_passthrough.cpp), and its task needs an Embassy executor this binary
///          does not have. So these checks pin the COLLECTOR's contract against a loader that
///          answers as the design says one should — which is worth having, and is not evidence that
///          the real loader answers that way.
///          The gate that runs the real Rust loader against this same C++ collector, over real
///          files, is `audio-load-ab` (harness/registry.toml -> scripts/audio_load_ab.sh ->
///          src/deluge/harness/audio_load_ab_scenario.cpp), on the Embassy host BSP. If a change
///          here alters what the collector does with a loader result, that is the gate to run.

#include "harness/load_invariants.h" // checkLoadInvariants -- shared with the Embassy A/B gate
#include "libdeluge/audio_load.h"
#include "memory/general_memory_allocator.h" // GeneralMemoryAllocator::resourceManager()
#include "model/sample/sample.h"
#include "storage/audio/audio_file_holder.h" // checkLoadFileDefaultStaysSynchronous exercises AudioFileHolder::loadFile directly
#include "storage/audio/audio_file_manager.h"
#include "storage/wave_table/wave_table.h"

#include "OSLikeStuff/scheduler_api.h" // TaskHandle
#include "deluge_resource.h"           // DelugeResourceStats, deluge_resource_stats(_reset)
#include "libdeluge/system.h"          // deluge_platform_init

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new> // placement new (the stand-in WaveTable in checkAsyncCollector)
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" int32_t deluge_main(void);
extern TaskHandle startupConditionalTask;

namespace {

// Files to load, captured from argv. `wavetable` selects AudioFileType::WAVETABLE.
struct LoadItem {
	std::string path;
	bool wavetable;
};
std::vector<LoadItem> g_items;

const char* errorName(Error e) {
	switch (e) {
	case Error::NONE:
		return "NONE";
	case Error::FILE_CORRUPTED:
		return "FILE_CORRUPTED";
	case Error::FILE_UNSUPPORTED:
		return "FILE_UNSUPPORTED";
	case Error::FILE_UNREADABLE:
		return "FILE_UNREADABLE";
	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE:
		return "NOT_WAVETABLE";
	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		return "NOT_WAVETABLE_STEREO";
	case Error::SD_CARD:
		return "SD_CARD";
	case Error::INSUFFICIENT_RAM:
		return "INSUFFICIENT_RAM";
	default:
		return "OTHER";
	}
}

// Plain CRC32 (IEEE 802.3, reflected) over a byte range — a cheap stable fingerprint of decoded band data.
uint32_t crc32(const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
		}
	}
	return ~crc;
}

// Print a WaveTable's decoded structure + a fingerprint of each band's data. The fingerprint covers the
// output of setup()'s read-then-FFT pipeline, so a regression in the cluster-streaming data read shows as a
// CRC diff. Band data is numCycles cycles of cycleSizeNoDuplicates 16-bit samples (duplicates excluded so the
// fingerprint doesn't depend on the WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE constant).
void dumpWaveTable(const char* path, const WaveTable& wt) {
	printf("LOADED %s | WAVETABLE channels=%u numCycles=%d numBands=%zu", path, wt.numChannels, wt.numCycles,
	       static_cast<size_t>(wt.bands.size()));
	for (size_t b = 0; b < wt.bands.size(); b++) {
		const WaveTableBand& band = wt.bands[b];
		const size_t numSamples = static_cast<size_t>(wt.numCycles) * band.cycleSizeNoDuplicates;
		const uint32_t crc =
		    band.dataAccessAddress != nullptr ? crc32(band.dataAccessAddress, numSamples * sizeof(int16_t)) : 0;
		printf(" | band%zu cycleSize=%u mag=%u crc=%08X", b, band.cycleSizeNoDuplicates, band.cycleSizeMagnitude, crc);
	}
	printf("\n");
}

// Print one Sample's full parsed descriptor as a stable, greppable line.
void dumpSample(const char* path, const Sample& s) {
	printf("LOADED %s | channels=%u byteDepth=%u rawFormat=%u sampleRate=%u "
	       "dataStart=%u dataLen=%llu lengthSamples=%llu midiNote=%.4f loopStart=%u loopEnd=%u wtCycle=%u\n",
	       path, s.numChannels, s.byteDepth, static_cast<unsigned>(s.rawDataFormat), s.sampleRate,
	       s.audioDataStartPosBytes, static_cast<unsigned long long>(s.audioDataLengthBytes),
	       static_cast<unsigned long long>(s.lengthInSamples), static_cast<double>(s.midiNoteFromFile),
	       s.fileLoopStartSamples, s.fileLoopEndSamples, s.waveTableCycleSize);
}

/// Pin proposeLoadCandidates()'s policy (which paths a load tries, in what order — no I/O). A later
/// async loader consumes this function as its only source of truth for candidate paths, so its order
/// is behaviour: get it wrong and a load silently opens the wrong file (or misses one it used to find).
bool checkProposeLoadCandidates() {
	bool ok = true;
	using Candidates = std::array<std::string, AudioFileManager::kMaxLoadCandidates>;

	// proposes the alternate directory (bare-name form) before the regular path when it exists
	{
		// Order is behaviour: resolveFileSize tries the alternate dir first whenever it exists, and
		// the async loader opens candidates in the order this function returns them. For a single-level
		// "SAMPLES/..." path the flattened and bare alternate names coincide, so this exercises the
		// 2-distinct-candidate shape (duplicate alt name, then the regular path).
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("SAMPLES/kick.wav", c.data(), c.size());
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
	// path -- this is the case a two-candidate policy previously dropped (the bare-name-in-alt-dir
	// fallback), so it's the one that most needs pinning.
	{
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("SAMPLES/FOLDER/kick.wav", c.data(), c.size());
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
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("PRESETS/SYNTHS/kick.xml", c.data(), c.size());
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
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("SAMPLES/kick.wav", c.data(), c.size());
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

	// Leave the manager in its default (no-alternate-dir-known) state for the real loads below.
	audioFileManager.alternateAudioFileLoadPath.clear();
	audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;

	return ok;
}

/// Pin the C ABI build's contracts (libdeluge/audio_load.h): abandoning a build must not publish it,
/// and must not leave it sitting in the manager half-built (adopted, reasoned, zero geometry) -- see
/// deluge_audio_load_begin's doc comment; apply_geometry() must refuse an invalid raw_data_format
/// rather than silently accept it. No I/O, no card fixture: exercises deluge_audio_load_begin/_abandon/
/// _apply_geometry directly.
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
	//      AudioFileManager::adoptAudioFileObject's FREEZE_WITH_ERROR("RAF1") (logged, then a
	//      null-display-pointer fault in this no-boot mode): a hard, unmistakable crash rather than a
	//      silent pass.
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

/// Write a minimal, genuinely parseable WAV: 44-byte canonical header, mono, 16-bit, 44100 Hz, then
/// @p dataBytes bytes of data filled with @p fill. Both load paths have to be able to read these for
/// real -- the synchronous one parses the header, and the async one opens the file to bind its read
/// stream -- and the fill makes two otherwise identical files tell each other apart when read back.
/// @return true if the file was written.
bool writeTestWav(const std::string& path, uint32_t dataBytes, uint8_t fill) {
	constexpr uint32_t kHeaderBytes = 44;
	constexpr uint32_t kSampleRate = 44100;
	constexpr uint16_t kChannels = 1;
	constexpr uint16_t kBitsPerSample = 16;
	const uint16_t blockAlign = kChannels * (kBitsPerSample / 8);

	std::vector<uint8_t> bytes;
	bytes.reserve(kHeaderBytes + dataBytes);
	const auto put = [&](const void* src, size_t n) {
		const auto* p = static_cast<const uint8_t*>(src);
		bytes.insert(bytes.end(), p, p + n);
	};
	const auto putU32 = [&](uint32_t v) { put(&v, sizeof(v)); };
	const auto putU16 = [&](uint16_t v) { put(&v, sizeof(v)); };

	put("RIFF", 4);
	putU32(36 + dataBytes);
	put("WAVE", 4);
	put("fmt ", 4);
	putU32(16);
	putU16(1); // PCM
	putU16(kChannels);
	putU32(kSampleRate);
	putU32(kSampleRate * blockAlign);
	putU16(blockAlign);
	putU16(kBitsPerSample);
	put("data", 4);
	putU32(dataBytes);
	bytes.insert(bytes.end(), dataBytes, fill);

	FILE* f = fopen(path.c_str(), "wb");
	if (f == nullptr) {
		return false;
	}
	const bool written = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	fclose(f);
	return written;
}

/// Write @p bytes bytes that are deliberately NOT audio: the file opens, and then fails to parse.
/// @return true if the file was written.
bool writeGarbageFile(const std::string& path, uint32_t bytes) {
	const std::vector<uint8_t> junk(bytes, 0x7E);
	FILE* f = fopen(path.c_str(), "wb");
	if (f == nullptr) {
		return false;
	}
	const bool written = fwrite(junk.data(), 1, junk.size(), f) == junk.size();
	fclose(f);
	return written;
}

/// The async loader, faked. This binary has no Rust loader, so libdeluge/audio_load.h's
/// request/collect pair resolves to audio_load_abi.cpp's weak "there is no loader here" fallbacks;
/// the strong definitions below override them for this binary so the collector's four outcomes
/// (READY-with-a-verdict, READY-with-geometry, PENDING, UNKNOWN) can each be driven deliberately.
///
/// Inert until a test arms it: `active` false and `collectStatus` UNKNOWN reproduce exactly what the
/// weak fallbacks return, so every other mode of this binary -- in particular the real card loads
/// below -- behaves as if these were not linked at all.
struct AsyncLoaderStub {
	bool active = false;                               ///< what deluge_audio_load_async_active() reports
	uint8_t collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN; ///< what deluge_audio_load_collect() reports
	DelugeAudioLoadResult result{};                    ///< handed back on DELUGE_AUDIO_LOAD_READY
	bool requestAccepted = true;                       ///< what deluge_audio_load_request() reports
	int collectCalls = 0;
	int requestCalls = 0;
	std::string requestedPath;                    ///< the path of the last request
	std::vector<std::string> requestedCandidates; ///< the candidate list of the last request, in order
	/// Which path the armed result belongs to. Empty means "answer for whatever is asked", which is
	/// what the single-sample cases want; set (by resolveStubbedRequest) it keys the result by path
	/// the way the loader's own table does, so a load of several samples cannot collect its
	/// predecessor's result.
	std::string armedPath;
	/// When set, a request RESOLVES itself against this directory the way the real loader resolves
	/// against the card -- first candidate that exists wins -- and arms the READY result for it. That
	/// is what makes the candidate ORDER observable end to end: a stub that always handed back a
	/// pre-baked result would agree with any order at all.
	std::string resolveAgainstRoot;
};
AsyncLoaderStub g_asyncStub;

/// Resolve `requestedCandidates` against `resolveAgainstRoot` exactly as the loader resolves against
/// the card, and arm the result the next collect hands back: the first candidate that exists wins, its
/// size and (known) geometry are reported, and `alternate_path` is set only when the winner is not the
/// nominal path -- the same rule audio_loader.rs applies. If nothing exists, the loader's
/// FILE_UNREADABLE verdict.
void resolveStubbedRequest() {
	DelugeAudioLoadResult result{};
	result.error = static_cast<uint8_t>(Error::FILE_UNREADABLE);

	for (const std::string& candidate : g_asyncStub.requestedCandidates) {
		struct stat st{};
		const std::string full = g_asyncStub.resolveAgainstRoot + "/" + candidate;
		if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
			continue;
		}
		result = {};
		// A candidate that OPENS is the one the loader parses -- it never tries another. So a file
		// that is not audio comes back as a parse verdict, exactly as the real parser reports it, and
		// NOT as "nothing opened": the difference is what decides whether the collector may go looking
		// in the alternate dir.
		FILE* f = fopen(full.c_str(), "rb");
		char magic[4] = {};
		const bool isRiff = f != nullptr && fread(magic, 1, sizeof(magic), f) == sizeof(magic)
		                    && std::memcmp(magic, "RIFF", sizeof(magic)) == 0;
		if (f != nullptr) {
			fclose(f);
		}
		if (!isRiff) {
			result.error = static_cast<uint8_t>(Error::FILE_UNSUPPORTED);
			break;
		}
		result.size_bytes = static_cast<uint64_t>(st.st_size);
		result.error = static_cast<uint8_t>(Error::NONE);
		// The geometry of the WAVs this harness writes (see writeTestWav): 44-byte header, mono,
		// 16-bit, 44100 Hz.
		result.geometry.sample_rate = 44100;
		result.geometry.audio_data_start_pos_bytes = 44;
		result.geometry.audio_data_length_bytes = static_cast<uint32_t>(st.st_size) - 44;
		result.geometry.num_channels = 1;
		result.geometry.byte_depth = 2;
		result.geometry.raw_data_format = 0; // RawDataFormat::NATIVE
		if (candidate != g_asyncStub.requestedPath) {
			std::snprintf(result.alternate_path, DELUGE_MAX_PATH, "%s", candidate.c_str());
		}
		break;
	}

	g_asyncStub.result = result;
	g_asyncStub.armedPath = g_asyncStub.requestedPath;
	g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
}

} // namespace

extern "C" bool deluge_audio_load_async_active() noexcept {
	return g_asyncStub.active;
}

extern "C" uint8_t deluge_audio_load_collect(const char* file_path, DelugeAudioLoadResult* out) noexcept {
	g_asyncStub.collectCalls++;
	if (g_asyncStub.collectStatus != DELUGE_AUDIO_LOAD_READY) {
		return g_asyncStub.collectStatus;
	}
	if (!g_asyncStub.armedPath.empty() && g_asyncStub.armedPath != file_path) {
		return DELUGE_AUDIO_LOAD_UNKNOWN; // a result for a different path is not this path's
	}
	*out = g_asyncStub.result;
	if (!g_asyncStub.armedPath.empty()) {
		// Taken exactly once, as the loader's table does: a second collect reports UNKNOWN.
		g_asyncStub.armedPath.clear();
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
	}
	return DELUGE_AUDIO_LOAD_READY;
}

extern "C" bool deluge_audio_load_request(const char* file_path, const char* candidates,
                                          uint32_t num_candidates) noexcept {
	g_asyncStub.requestCalls++;
	g_asyncStub.requestedPath = file_path;
	g_asyncStub.requestedCandidates.clear();
	for (uint32_t i = 0; i < num_candidates; i++) {
		g_asyncStub.requestedCandidates.emplace_back(candidates + (i * DELUGE_MAX_PATH));
	}
	if (!g_asyncStub.resolveAgainstRoot.empty()) {
		resolveStubbedRequest();
	}
	return g_asyncStub.requestAccepted;
}

namespace {

/// Pin the C++ collector: the miss path AudioFileManager::getAudioFileFromFilename takes when a
/// caller opts into the async loader (mayLoadAsync) and the loader is backing loads.
///
/// Needs no app boot and no card fixture -- it serves its own two-file "card" out of a temp directory
/// (DELUGE_SD_ROOT), which is what lets the one genuinely blocking step, binding the Sample's read
/// stream, actually succeed here.
bool checkAsyncCollector() {
	bool ok = true;

	// A temp directory standing in for the card: SAMPLES/ASYNC.WAV is what a successful build opens
	// (its CONTENT is never parsed -- the collector's geometry comes from the loader's result, not from
	// the file), and SAMPLES/ALT/ exists so the alternate-load-dir probe resolves to DOES_EXIST.
	char rootTemplate[] = "/tmp/deluge_loadcheck_XXXXXX";
	const char* root = mkdtemp(rootTemplate);
	if (root == nullptr) {
		printf("FAIL [asyncCollector]: could not create a temp card directory\n");
		return false;
	}
	const std::string samplesDir = std::string{root} + "/SAMPLES";
	const std::string altDir = samplesDir + "/ALT";
	const std::string samplePath = samplesDir + "/ASYNC.WAV";
	mkdir(samplesDir.c_str(), 0755);
	mkdir(altDir.c_str(), 0755);
	constexpr uint32_t kSampleFileSize = 4096;
	// Removes whatever of the temp card exists, on the failure exit below as well as at the end -- an
	// early return must not leave a directory behind in /tmp.
	const auto removeTempCard = [&] {
		unlink(samplePath.c_str());
		rmdir(altDir.c_str());
		rmdir(samplesDir.c_str());
		rmdir(root);
	};
	{
		std::vector<char> bytes(kSampleFileSize, 0);
		FILE* f = fopen(samplePath.c_str(), "wb");
		if (f == nullptr || fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
			printf("FAIL [asyncCollector]: could not write the temp card's sample file\n");
			if (f != nullptr) {
				fclose(f);
			}
			removeTempCard();
			return false;
		}
		fclose(f);
	}
	// Point the host passthrough at the temp card for the duration, then put back whatever was there
	// (the --project directory, in file-loading mode) so this leaves nothing behind it.
	const char* previousRootRaw = getenv("DELUGE_SD_ROOT");
	const std::string previousRoot = (previousRootRaw != nullptr) ? previousRootRaw : "";
	const bool hadPreviousRoot = previousRootRaw != nullptr;
	setenv("DELUGE_SD_ROOT", root, 1);

	// The geometry a READY result carries. Deliberately not all-default: every field is checked on the
	// published Sample below, so a collector that dropped or transposed one shows up as a value
	// mismatch rather than a plausible-looking zero.
	DelugeAudioLoadGeometry geometry{};
	geometry.sample_rate = 44100;
	geometry.audio_data_start_pos_bytes = 44;
	geometry.audio_data_length_bytes = kSampleFileSize - 44;
	geometry.file_loop_start_samples = 100;
	geometry.file_loop_end_samples = 200;
	geometry.wavetable_cycle_size = 2048;
	geometry.midi_note_value = 60.0f;
	geometry.num_channels = 1;
	geometry.byte_depth = 2;
	geometry.raw_data_format = 0; // RawDataFormat::NATIVE
	geometry.midi_note_present = 1;
	geometry.wavetable_explicitly_specifies_self = 0;

	const auto readyResult = [&](uint8_t error, const char* alternatePath) {
		DelugeAudioLoadResult result{};
		result.geometry = geometry;
		result.size_bytes = kSampleFileSize;
		result.error = error;
		std::snprintf(result.alternate_path, DELUGE_MAX_PATH, "%s", alternatePath);
		return result;
	};

	// With the loader inactive -- which is how it ships, and how this binary is built -- opting in must
	// change nothing at all: same result, same error, and the collector not even consulted. This is the
	// A/B that catches a miss path which forgot to check deluge_audio_load_async_active().
	{
		g_asyncStub = {};
		std::string syncPath = "SAMPLES/NOT_THERE.WAV";
		Error syncError = Error::NONE;
		AudioFile* syncFile =
		    audioFileManager.getAudioFileFromFilename(syncPath, /*mayReadCard=*/true, &syncError, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/false);
		std::string asyncPath = "SAMPLES/NOT_THERE.WAV";
		Error asyncError = Error::NONE;
		AudioFile* asyncFile = audioFileManager.getAudioFileFromFilename(asyncPath, /*mayReadCard=*/true, &asyncError,
		                                                                 AudioFileType::SAMPLE,
		                                                                 /*makeWaveTableWorkAtAllCosts=*/false,
		                                                                 /*mayLoadAsync=*/true);
		if (syncFile != asyncFile || syncError != asyncError || syncPath != asyncPath) {
			printf("FAIL [asyncCollector]: mayLoadAsync changed the synchronous miss path with the loader "
			       "inactive (file %p vs %p, error %d vs %d)\n",
			       static_cast<void*>(syncFile), static_cast<void*>(asyncFile), static_cast<int>(syncError),
			       static_cast<int>(asyncError));
			ok = false;
		}
		if (g_asyncStub.collectCalls != 0 || g_asyncStub.requestCalls != 0) {
			printf("FAIL [asyncCollector]: the loader was consulted while inactive (%d collect, %d request)\n",
			       g_asyncStub.collectCalls, g_asyncStub.requestCalls);
			ok = false;
		}
	}

	// A WaveTable never goes through the loader, even with everything else armed: it parses its file a
	// second time through the deserializer, which the sample-only build path does not do.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		std::string path = "SAMPLES/NOT_THERE.WAV";
		Error error = Error::NONE;
		(void)audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::WAVETABLE,
		                                                /*makeWaveTableWorkAtAllCosts=*/false, /*mayLoadAsync=*/true);
		if (g_asyncStub.collectCalls != 0) {
			printf("FAIL [asyncCollector]: a WAVETABLE load consulted the sample loader\n");
			ok = false;
		}
	}

	// UNKNOWN: the loader has never heard of this path, so ask it -- with the candidates the CURRENT
	// alternate-dir status proposes, and without resolving that status first. From MIGHT_EXIST (the
	// state every song and preset load starts in) that is the regular path alone, which is the one
	// candidate resolveFileSize tries first from the same state; the probe that unlocks the rest waits
	// until it has actually missed. Promoting the status here instead would change the try order for
	// every later sample of the load, including any that go down the synchronous path -- see
	// checkAsyncAlternateDirOrder, which runs a whole load to catch exactly that.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;

		const size_t before = audioFileManager.audioFiles.size();
		std::string path = "SAMPLES/FOLDER/kick.wav";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file != nullptr || error != Error::LOAD_IN_PROGRESS) {
			printf("FAIL [asyncCollector]: an UNKNOWN path should report LOAD_IN_PROGRESS, got file=%p error=%d\n",
			       static_cast<void*>(file), static_cast<int>(error));
			ok = false;
		}
		if (g_asyncStub.requestCalls != 1 || g_asyncStub.requestedPath != "SAMPLES/FOLDER/kick.wav") {
			printf("FAIL [asyncCollector]: expected one request for \"SAMPLES/FOLDER/kick.wav\", got %d for \"%s\"\n",
			       g_asyncStub.requestCalls, g_asyncStub.requestedPath.c_str());
			ok = false;
		}
		const std::vector<std::string> expected{"SAMPLES/FOLDER/kick.wav"};
		if (g_asyncStub.requestedCandidates != expected) {
			printf("FAIL [asyncCollector]: an unresolved alternate dir should propose the regular path alone "
			       "(got %zu:",
			       g_asyncStub.requestedCandidates.size());
			for (const std::string& c : g_asyncStub.requestedCandidates) {
				printf(" \"%s\"", c.c_str());
			}
			printf(")\n");
			ok = false;
		}
		if (audioFileManager.alternateLoadDirStatus != AlternateLoadDirStatus::MIGHT_EXIST) {
			printf("FAIL [asyncCollector]: requesting promoted the alternate-dir status (to %d) before anything "
			       "missed\n",
			       static_cast<int>(audioFileManager.alternateLoadDirStatus));
			ok = false;
		}
		if (audioFileManager.audioFiles.size() != before) {
			printf("FAIL [asyncCollector]: a request published something (%zu -> %zu)\n", before,
			       audioFileManager.audioFiles.size());
			ok = false;
		}
		audioFileManager.alternateAudioFileLoadPath.clear();
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	}

	// A request the loader drops (its queue is full) is still LOAD_IN_PROGRESS: the caller asks again
	// on a later tick, rather than falling back to a blocking load exactly when the loader is busiest.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
		g_asyncStub.requestAccepted = false;
		std::string path = "SAMPLES/REFUSED.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file != nullptr || error != Error::LOAD_IN_PROGRESS) {
			printf("FAIL [asyncCollector]: a dropped request should still report LOAD_IN_PROGRESS, got file=%p "
			       "error=%d\n",
			       static_cast<void*>(file), static_cast<int>(error));
			ok = false;
		}
	}

	// PENDING: queued or in flight. Reported as LOAD_IN_PROGRESS, and NOT re-requested -- the loader
	// already has this path, which is why the collector keeps no in-flight table of its own.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_PENDING;
		std::string path = "SAMPLES/PENDING.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file != nullptr || error != Error::LOAD_IN_PROGRESS) {
			printf("FAIL [asyncCollector]: a PENDING path should report LOAD_IN_PROGRESS, got file=%p error=%d\n",
			       static_cast<void*>(file), static_cast<int>(error));
			ok = false;
		}
		if (g_asyncStub.requestCalls != 0) {
			printf("FAIL [asyncCollector]: a PENDING path was re-requested (%d times)\n", g_asyncStub.requestCalls);
			ok = false;
		}
	}

	// READY carrying the loader's verdict on the file: reported with that exact identity (not a
	// generic failure), and nothing is built or published.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
		g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::FILE_UNSUPPORTED), "");
		const size_t before = audioFileManager.audioFiles.size();
		std::string path = "SAMPLES/UNSUPPORTED.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file != nullptr || error != Error::FILE_UNSUPPORTED) {
			printf("FAIL [asyncCollector]: a failed parse should surface as its own error, got file=%p error=%d\n",
			       static_cast<void*>(file), static_cast<int>(error));
			ok = false;
		}
		if (audioFileManager.audioFiles.size() != before) {
			printf("FAIL [asyncCollector]: a failed parse published something (%zu -> %zu)\n", before,
			       audioFileManager.audioFiles.size());
			ok = false;
		}
	}

	// READY with geometry: the whole build runs here -- begin, bind the read stream against the temp
	// card, apply the geometry, publish -- and the published Sample must carry every field the loader
	// parsed. A dropped or transposed field is the failure this boundary can produce silently.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
		g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::NONE), "");
		std::string path = "SAMPLES/ASYNC.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file == nullptr || error != Error::NONE) {
			printf("FAIL [asyncCollector]: a READY result did not build a Sample (file=%p error=%d)\n",
			       static_cast<void*>(file), static_cast<int>(error));
			ok = false;
		}
		else {
			Sample& s = static_cast<Sample&>(*file);
			const uint32_t expectedLengthSamples =
			    geometry.audio_data_length_bytes / (geometry.byte_depth * geometry.num_channels);
			if (s.sampleRate != geometry.sample_rate || s.audioDataStartPosBytes != geometry.audio_data_start_pos_bytes
			    || s.audioDataLengthBytes != geometry.audio_data_length_bytes
			    || s.fileLoopStartSamples != geometry.file_loop_start_samples
			    || s.fileLoopEndSamples != geometry.file_loop_end_samples
			    || s.waveTableCycleSize != geometry.wavetable_cycle_size || s.numChannels != geometry.num_channels
			    || s.byteDepth != geometry.byte_depth || s.midiNoteFromFile != geometry.midi_note_value
			    || s.lengthInSamples != expectedLengthSamples) {
				printf("FAIL [asyncCollector]: the published Sample's geometry does not match the collected "
				       "result: channels=%u byteDepth=%u rate=%u start=%u len=%llu loop=%u/%u wtCycle=%u "
				       "midiNote=%.4f lengthSamples=%llu\n",
				       s.numChannels, s.byteDepth, s.sampleRate, s.audioDataStartPosBytes,
				       static_cast<unsigned long long>(s.audioDataLengthBytes), s.fileLoopStartSamples,
				       s.fileLoopEndSamples, s.waveTableCycleSize, static_cast<double>(s.midiNoteFromFile),
				       static_cast<unsigned long long>(s.lengthInSamples));
				ok = false;
			}
			// The read stream is checked by USING it: a published build whose stream was never bound
			// keeps handle 0, and every read against handle 0 fails cleanly -- which is exactly why an
			// unbound sample loads with no error and then plays silence forever.
			{
				std::array<std::byte, 16> probe{};
				auto readSource = s.stream().make_read_source();
				if (readSource == nullptr || !readSource->read(0, probe).has_value()) {
					printf("FAIL [asyncCollector]: the published Sample cannot be read -- its stream was never "
					       "bound, so it would load without error and play silence\n");
					ok = false;
				}
			}

			// Resident now: a second look-up must be answered from memory, without going near the loader.
			const int collectsBefore = g_asyncStub.collectCalls;
			std::string againPath = "SAMPLES/ASYNC.WAV";
			Error againError = Error::NONE;
			AudioFile* again = audioFileManager.getAudioFileFromFilename(againPath, /*mayReadCard=*/true, &againError,
			                                                             AudioFileType::SAMPLE,
			                                                             /*makeWaveTableWorkAtAllCosts=*/false,
			                                                             /*mayLoadAsync=*/true);
			if (again != file || againError != Error::NONE || g_asyncStub.collectCalls != collectsBefore) {
				printf("FAIL [asyncCollector]: a resident sample was not served from memory (file=%p vs %p, "
				       "error=%d, %d extra collect(s))\n",
				       static_cast<void*>(again), static_cast<void*>(file), static_cast<int>(againError),
				       g_asyncStub.collectCalls - collectsBefore);
				ok = false;
			}
		}

		if (!checkLoadInvariants("asyncCollector/published")) {
			ok = false;
		}
	}

	// A path the loader can NEVER accept must fall through to the synchronous path, not sit in
	// LOAD_IN_PROGRESS. deluge_audio_load_request refuses a non-UTF-8 path and one that does not fit
	// its fixed-capacity buffer, and neither refusal can change on a later tick -- a caller told
	// LOAD_IN_PROGRESS would re-request into the same refusal forever, so the load would never
	// complete and never fail. The synchronous path has neither limit, which is why declining is the
	// answer rather than an error.
	{
		struct InadmissibleCase {
			const char* label;
			std::string path;
		};
		// 0xFF can never appear in valid UTF-8 in any position; 300 characters is past the 255 the
		// loader's fixed-capacity path can hold.
		const InadmissibleCase cases[] = {
		    {"a non-UTF-8 path", std::string{"SAMPLES/BAD"} + static_cast<char>(0xFF) + "NAME.WAV"},
		    {"an over-long path", "SAMPLES/" + std::string(300, 'A') + ".WAV"},
		};
		for (const InadmissibleCase& c : cases) {
			g_asyncStub = {};
			g_asyncStub.active = true;
			g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
			std::string path = c.path;
			Error error = Error::NONE;
			AudioFile* file =
			    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
			                                              /*makeWaveTableWorkAtAllCosts=*/false,
			                                              /*mayLoadAsync=*/true);
			if (file != nullptr || error == Error::LOAD_IN_PROGRESS) {
				printf("FAIL [asyncCollector]: %s must fall through to the synchronous path, not wait forever "
				       "for a request that is refused permanently (file=%p error=%d)\n",
				       c.label, static_cast<void*>(file), static_cast<int>(error));
				ok = false;
			}
			if (g_asyncStub.requestCalls != 0) {
				printf("FAIL [asyncCollector]: %s was requested anyway (%d time(s))\n", c.label,
				       g_asyncStub.requestCalls);
				ok = false;
			}
		}
	}

	// A retry that cannot be formed must fall THROUGH to the synchronous path, not answer the call
	// with a failure. The retry's candidates are new ones -- the alternate dir's -- so they can be
	// inadmissible where the first request's were fine: here the alternate load dir's own name is not
	// UTF-8, which a FAT long filename is free to be. The sample exists only in that directory, so a
	// load that stops at the refused re-request loses a file the synchronous path finds.
	{
		const std::string oddAltDir = samplesDir + "/\xFFALT";
		const std::string oddSample = oddAltDir + "/ODD.WAV";
		mkdir(oddAltDir.c_str(), 0755);
		if (!writeTestWav(oddSample, 1200, 0x77)) {
			printf("FAIL [asyncCollector]: could not write the non-UTF-8 alternate dir's sample\n");
			ok = false;
		}
		else {
			g_asyncStub = {};
			g_asyncStub.active = true;
			g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
			// "Nothing opened" -- which is what earns the alternate-dir probe and the re-request.
			g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::FILE_UNREADABLE), "");
			audioFileManager.alternateAudioFileLoadPath = "SAMPLES/\xFFALT";
			audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
			audioFileManager.thingTypeBeingLoaded = ThingType::SONG;

			std::string path = "SAMPLES/ODD.WAV";
			Error error = Error::NONE;
			AudioFile* file =
			    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
			                                              /*makeWaveTableWorkAtAllCosts=*/false,
			                                              /*mayLoadAsync=*/true);
			if (file == nullptr || error != Error::NONE) {
				printf("FAIL [asyncCollector]: a re-request that could not be formed answered the load instead "
				       "of falling through to the synchronous path (file=%p error=%d)\n",
				       static_cast<void*>(file), static_cast<int>(error));
				ok = false;
			}
			else if (static_cast<Sample&>(*file).loadedFromAlternatePath != "SAMPLES/\xFFALT/ODD.WAV") {
				printf("FAIL [asyncCollector]: the fallen-through load did not come from the alternate dir "
				       "(alt=\"%s\")\n",
				       static_cast<Sample&>(*file).loadedFromAlternatePath.c_str());
				ok = false;
			}
			audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/ODD.WAV");
			audioFileManager.alternateAudioFileLoadPath.clear();
			audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
			audioFileManager.thingTypeBeingLoaded = ThingType::NONE;
		}
		unlink(oddSample.c_str());
		rmdir(oddAltDir.c_str());
	}

	// The two gate conjuncts that are invisible with the loader off, and would stay green through a
	// refactor that dropped them. Both are about NOT taking the async path.
	{
		// mayReadCard=false means "look in memory only". A miss must stay the silent absence it has
		// always been (nullptr, *error untouched) rather than becoming card I/O on another tier.
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
		g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::NONE), "");
		std::string path = "SAMPLES/NOT_RESIDENT.WAV"; // a miss, so the gate is what decides
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/false, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file != nullptr || error != Error::NONE || g_asyncStub.collectCalls != 0) {
			printf("FAIL [asyncCollector]: mayReadCard=false reached the loader (file=%p error=%d %d collect(s))\n",
			       static_cast<void*>(file), static_cast<int>(error), g_asyncStub.collectCalls);
			ok = false;
		}
	}
	{
		// A card disabled until reboot (its cluster size grew past what this boot sized its buffers
		// for) must keep reporting SD_CARD from the synchronous path. Requesting instead would hand the
		// loader a card the app has already ruled unusable, and report LOAD_IN_PROGRESS forever.
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
		g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::NONE), "");
		audioFileManager.cardDisabled = true;
		std::string path = "SAMPLES/DISABLED.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		audioFileManager.cardDisabled = false;
		if (file != nullptr || error != Error::SD_CARD || g_asyncStub.collectCalls != 0) {
			printf("FAIL [asyncCollector]: a disabled card should report SD_CARD from the synchronous path "
			       "(file=%p error=%d %d collect(s))\n",
			       static_cast<void*>(file), static_cast<int>(error), g_asyncStub.collectCalls);
			ok = false;
		}
	}

	// A Sample and a WaveTable of the same file share a path and sit adjacent in the sorted vector, so
	// a search for that path can land on either. Put a WaveTable there first, then collect a Sample at
	// the same path: the collector must hand back the SAMPLE, and so must the resident lookup that
	// answers every later request for it.
	//
	// The WaveTable is constructed and published directly rather than loaded, because a real wavetable
	// load cannot run in this mode at all: WaveTable::setup builds an FFT config, and allocating one
	// pumps the audio engine (routineWithClusterLoading), which faults with no booted host behind it.
	// It stands in perfectly for what this branch actually reads -- an AudioFile of the other type at
	// the same path -- since nothing here touches its band data.
	{
		g_asyncStub = {};
		const char* const sharedPath = "SAMPLES/SHARED.WAV";
		void* waveTableMemory = deluge::memory::alloc_external(sizeof(WaveTable), 16);
		auto* waveTable = (waveTableMemory != nullptr) ? new (waveTableMemory) WaveTable : nullptr;
		if (waveTable == nullptr) {
			printf("FAIL [asyncCollector]: could not allocate the stand-in WaveTable\n");
			ok = false;
		}
		else {
			waveTable->filePath = sharedPath;
			audioFileManager.adoptAudioFileObject(waveTable);
			const auto inserted = audioFileManager.audioFiles.insertElement(waveTable);
			if (!inserted) {
				printf("FAIL [asyncCollector]: could not publish the stand-in WaveTable\n");
				audioFileManager.destroyAudioFileObject(*waveTable);
				ok = false;
			}
			else {
				g_asyncStub.active = true;
				g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
				g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::NONE), "SAMPLES/ASYNC.WAV");
				std::string path = sharedPath;
				Error error = Error::NONE;
				AudioFile* sample =
				    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
				                                              /*makeWaveTableWorkAtAllCosts=*/false,
				                                              /*mayLoadAsync=*/true);
				if (sample == nullptr || error != Error::NONE || sample->type != AudioFileType::SAMPLE) {
					printf("FAIL [asyncCollector]: collecting a SAMPLE where a WaveTable is resident at the same "
					       "path did not produce a Sample (file=%p type=%d error=%d)\n",
					       static_cast<void*>(sample), sample != nullptr ? static_cast<int>(sample->type) : -1,
					       static_cast<int>(error));
					ok = false;
				}
				else {
					// Both types are resident at this path now, so the resident lookup has to pick the
					// right neighbour -- the branch a plain "return the search hit" would get wrong.
					const int collectsBefore = g_asyncStub.collectCalls;
					std::string residentPath = sharedPath;
					Error residentError = Error::NONE;
					AudioFile* residentSample = audioFileManager.getAudioFileFromFilename(
					    residentPath, /*mayReadCard=*/true, &residentError, AudioFileType::SAMPLE,
					    /*makeWaveTableWorkAtAllCosts=*/false,
					    /*mayLoadAsync=*/true);
					if (residentSample != sample || g_asyncStub.collectCalls != collectsBefore) {
						printf("FAIL [asyncCollector]: the resident SAMPLE at a path a WaveTable also occupies was "
						       "not returned (file=%p vs %p, type=%d, %d extra collect(s))\n",
						       static_cast<void*>(residentSample), static_cast<void*>(sample),
						       residentSample != nullptr ? static_cast<int>(residentSample->type) : -1,
						       g_asyncStub.collectCalls - collectsBefore);
						ok = false;
					}
					std::string waveTablePath = sharedPath;
					Error waveTableError = Error::NONE;
					AudioFile* residentWaveTable = audioFileManager.getAudioFileFromFilename(
					    waveTablePath, /*mayReadCard=*/true, &waveTableError, AudioFileType::WAVETABLE,
					    /*makeWaveTableWorkAtAllCosts=*/false,
					    /*mayLoadAsync=*/false);
					if (residentWaveTable != waveTable) {
						printf("FAIL [asyncCollector]: the resident WAVETABLE at that same path was not returned "
						       "(file=%p vs %p)\n",
						       static_cast<void*>(residentWaveTable), static_cast<void*>(waveTable));
						ok = false;
					}
				}
			}
		}
		// Both entries share one path, so this runs twice -- once per resident object.
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(sharedPath);
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(sharedPath);
	}

	// A build that cannot be closed out cleanly must still be closed out. Two failures reach that:
	// the read-stream bind failing (the file the loader resolved is gone by the time we open it) and
	// the geometry being refused (an out-of-range raw_data_format). Both must abandon the build --
	// which is invisible in audioFiles, since begin() never inserts there, so the check is the same
	// pair checkAudioLoadAbiAbandon() uses: enough churn to exhaust the chunk table if a lease leaks,
	// and zero pressure-driven evictions if the object leaks unleased.
	{
		DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
		if (mgr != nullptr) {
			deluge_resource_stats_reset(mgr);
		}

		struct OwnershipCase {
			const char* label;
			const char* path;          ///< the nominal path the load asks for
			const char* alternatePath; ///< where the result says the bytes are ("" = at `path`)
			uint8_t rawDataFormat;     ///< 200 = not one of RawDataFormat's values, so it is refused
			Error expectedError;
			int iterations; ///< see below for why only one case carries the churn
		};
		// The bind case names a file that is not on the temp card at all; the geometry case resolves to
		// the one that is, so its bind succeeds and the refused geometry is what ends the build. The
		// expected error is asserted on EVERY iteration, not just the first: a build that leaked its
		// read handle would start failing with TOO_MANY_OPEN_STREAMS partway through, which a
		// "some error, any error" check would read as a pass.
		//
		// Only the geometry case carries the chunk-table churn, and that is enough: both cases end in
		// the same deluge_audio_load_abandon, and the geometry one abandons the HEAVIER build (its
		// bind succeeded, so it also has a read handle to release). The bind case runs a few times
		// purely for its error identity -- each of its iterations logs a failed open, and 20,000 of
		// those would bury the gate's own output.
		const OwnershipCase cases[] = {
		    {"bind failure", "SAMPLES/GONE.WAV", "", 0, Error::FILE_NOT_FOUND, 3},
		    {"refused geometry", "SAMPLES/ASYNC_BADFORMAT.WAV", "SAMPLES/ASYNC.WAV", 200, Error::BUG, 20000},
		};

		for (const OwnershipCase& c : cases) {
			for (int i = 0; i < c.iterations; i++) {
				g_asyncStub = {};
				g_asyncStub.active = true;
				g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
				g_asyncStub.result = readyResult(static_cast<uint8_t>(Error::NONE), c.alternatePath);
				g_asyncStub.result.geometry.raw_data_format = c.rawDataFormat;

				std::string path = c.path;
				Error error = Error::NONE;
				AudioFile* file =
				    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
				                                              /*makeWaveTableWorkAtAllCosts=*/false,
				                                              /*mayLoadAsync=*/true);
				if (file != nullptr || error != c.expectedError) {
					printf("FAIL [asyncCollector]: a %s must fail the load with error=%d; iteration %d got "
					       "file=%p error=%d\n",
					       c.label, static_cast<int>(c.expectedError), i, static_cast<void*>(file),
					       static_cast<int>(error));
					ok = false;
					break;
				}
			}
		}

		if (mgr != nullptr) {
			DelugeResourceStats stats{};
			deluge_resource_stats(mgr, &stats);
			if (stats.evictions != 0) {
				printf("FAIL [asyncCollector]: %llu pressure-driven eviction(s) during failed-build churn that "
				       "should never have needed any -- a failed build is leaking its Sample\n",
				       static_cast<unsigned long long>(stats.evictions));
				ok = false;
			}
		}
	}

	g_asyncStub = {};
	// Nothing this check published may outlive the temp card it was loaded from: a resident Sample
	// holds one of the streaming registry's scarce handles for the rest of the run, on a file the next
	// few lines delete.
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/ASYNC.WAV");
	removeTempCard();
	if (hadPreviousRoot) {
		setenv("DELUGE_SD_ROOT", previousRoot.c_str(), 1);
	}
	else {
		unsetenv("DELUGE_SD_ROOT");
	}
	return ok;
}

/// Pin that every existing loadFile-family call site -- all but previewSample's, which is the one this
/// task changed to pass true -- keeps taking the synchronous backing when mayLoadAsync is left at its
/// default, on BOTH layers that default it. `AudioFileHolder::loadFile` forwards its own mayLoadAsync
/// argument straight to `AudioFileManager::getAudioFileFromFilename` (see audio_file_holder.cpp), and
/// each has its own `= false` default -- so a regression in EITHER one has to be independently
/// reachable from this check, or it is not actually guarding that default.
///
/// Two layers, two calling shapes:
///  - AudioFileHolder::loadFile is the level nine call sites go through (slicer.cpp x2,
///    sample_browser.cpp's single-sample path, instrument_clip_view.cpp, audio_clip.cpp,
///    consequence_audio_clip_set_sample.cpp, source.cpp, and audio_engine.cpp's previewSample).
///  - getAudioFileFromFilename is called DIRECTLY, bypassing the holder, by a tenth site:
///    sample_browser.cpp's folder-load path (`loadAllSamplesInFolder`), which has no AudioFileHolder to
///    go through at all. A check that only exercises the holder would leave this one, and its own
///    default, completely unwatched.
///
/// The async gate is `mayLoadAsync && ... && deluge_audio_load_async_active()`: with the loader
/// INACTIVE (the shipping default), that whole conjunction is false regardless of what mayLoadAsync
/// holds, so a check that never arms the stub cannot tell "the default is false" from "the default is
/// true but nothing is listening" -- it would pass either way. This check therefore arms
/// `g_asyncStub.active = true` itself (checkAsyncCollector, which runs immediately before this in the
/// gate list, resets the stub to inactive on its own exit, and this check resets it again on ITS OWN
/// exit so nothing after it inherits an armed loader either), and for each layer runs the SAME file
/// through the SAME active stub twice: once with mayLoadAsync omitted (must stay synchronous, zero
/// loader calls), and once with it explicit `true` (must reach the loader and report
/// LOAD_IN_PROGRESS). That contrast, run per layer, is what makes the check able to fail: flip either
/// default to `true` on its own and the corresponding omitted-argument call starts behaving like the
/// explicit one.
bool checkLoadFileDefaultStaysSynchronous() {
	bool ok = true;

	char rootTemplate[] = "/tmp/deluge_loadcheck_XXXXXX";
	const char* root = mkdtemp(rootTemplate);
	if (root == nullptr) {
		printf("FAIL [loadFileDefault]: could not create a temp card directory\n");
		return false;
	}
	const std::string samplesDir = std::string{root} + "/SAMPLES";
	const std::string samplePath = samplesDir + "/DEFAULT.WAV";
	mkdir(samplesDir.c_str(), 0755);
	const auto removeTempCard = [&] {
		unlink(samplePath.c_str());
		rmdir(samplesDir.c_str());
		rmdir(root);
	};
	if (!writeTestWav(samplePath, 2000, 0x42)) {
		printf("FAIL [loadFileDefault]: could not write the temp card's sample file\n");
		removeTempCard();
		return false;
	}

	const char* previousRootRaw = getenv("DELUGE_SD_ROOT");
	const std::string previousRoot = (previousRootRaw != nullptr) ? previousRootRaw : "";
	const bool hadPreviousRoot = previousRootRaw != nullptr;
	setenv("DELUGE_SD_ROOT", root, 1);

	// ---- Layer 1: AudioFileHolder::loadFile ----

	// Arm the loader as active -- see the doc comment above for why this is required for the check to
	// be able to fail at all.
	g_asyncStub = {};
	g_asyncStub.active = true;

	// mayLoadAsync omitted from AudioFileHolder::loadFile itself, exactly as the nine untouched call
	// sites invoke it. Even with the loader active, it must still take the synchronous backing and
	// never touch the loader at all.
	{
		AudioFileHolder holder;
		holder.filePath = "SAMPLES/DEFAULT.WAV";
		holder.audioFileType = AudioFileType::SAMPLE;
		Error error = holder.loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true);
		if (error == Error::LOAD_IN_PROGRESS) {
			printf("FAIL [loadFileDefault]: AudioFileHolder::loadFile's default reached the async path "
			       "with the loader active (Error::LOAD_IN_PROGRESS)\n");
			ok = false;
		}
		if (holder.audioFile == nullptr) {
			printf("FAIL [loadFileDefault]: AudioFileHolder::loadFile's default did not load the file "
			       "(error=%d)\n",
			       static_cast<int>(error));
			ok = false;
		}
		if (g_asyncStub.requestCalls != 0 || g_asyncStub.collectCalls != 0) {
			printf("FAIL [loadFileDefault]: AudioFileHolder::loadFile's default (mayLoadAsync omitted) "
			       "consulted the async loader anyway (%d request(s), %d collect(s))\n",
			       g_asyncStub.requestCalls, g_asyncStub.collectCalls);
			ok = false;
		}
		if (holder.audioFile != nullptr) {
			// Drop the reason setAudioFile took (addReason()) -- the same tag setAudioFile itself uses
			// when releasing one -- so the file can be freed before the contrast call below; otherwise
			// it stays resident and that call short-circuits on the in-memory search instead of
			// reaching the async gate at all.
			holder.audioFile->removeReason("E391");
		}
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DEFAULT.WAV");

	// The contrast: the SAME file, the SAME active stub, through AudioFileHolder::loadFile again,
	// differing only in mayLoadAsync being explicit true instead of omitted. This MUST reach the loader
	// and report LOAD_IN_PROGRESS -- if it doesn't, the call above proved nothing, because the async
	// branch would be unreachable either way.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		AudioFileHolder holder;
		holder.filePath = "SAMPLES/DEFAULT.WAV";
		holder.audioFileType = AudioFileType::SAMPLE;
		Error error = holder.loadFile(/*reversed=*/false, /*manuallySelected=*/true, /*mayActuallyReadFile=*/true,
		                              CLUSTER_ENQUEUE, /*makeWaveTableWorkAtAllCosts=*/false,
		                              /*mayLoadAsync=*/true);
		if (holder.audioFile != nullptr || error != Error::LOAD_IN_PROGRESS) {
			printf("FAIL [loadFileDefault]: AudioFileHolder::loadFile(..., mayLoadAsync=true) did not "
			       "reach the async path with the loader active (audioFile=%p error=%d) -- the "
			       "default-vs-explicit contrast this check exists to see is broken\n",
			       static_cast<void*>(holder.audioFile), static_cast<int>(error));
			ok = false;
		}
		if (g_asyncStub.requestCalls != 1) {
			printf("FAIL [loadFileDefault]: expected exactly one async request for the explicit "
			       "mayLoadAsync=true call (holder layer), got %d\n",
			       g_asyncStub.requestCalls);
			ok = false;
		}
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DEFAULT.WAV");

	// ---- Layer 2: AudioFileManager::getAudioFileFromFilename, called directly ----
	//
	// sample_browser.cpp's folder-load path (loadAllSamplesInFolder) calls this with no
	// AudioFileHolder in between and no mayLoadAsync argument at all -- so its own default has to be
	// exercised here too, not just inferred from the holder's forwarding.

	// mayLoadAsync omitted, exactly as that call site invokes it. Even with the loader active, it must
	// still take the synchronous backing and never touch the loader at all.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		std::string path = "SAMPLES/DEFAULT.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE);
		if (error == Error::LOAD_IN_PROGRESS) {
			printf("FAIL [loadFileDefault]: getAudioFileFromFilename's default reached the async path "
			       "with the loader active (Error::LOAD_IN_PROGRESS)\n");
			ok = false;
		}
		if (file == nullptr) {
			printf("FAIL [loadFileDefault]: getAudioFileFromFilename's default did not load the file "
			       "(error=%d)\n",
			       static_cast<int>(error));
			ok = false;
		}
		if (g_asyncStub.requestCalls != 0 || g_asyncStub.collectCalls != 0) {
			printf("FAIL [loadFileDefault]: getAudioFileFromFilename's default (mayLoadAsync omitted) "
			       "consulted the async loader anyway (%d request(s), %d collect(s))\n",
			       g_asyncStub.requestCalls, g_asyncStub.collectCalls);
			ok = false;
		}
		// Returned "held by no reason" per its own doc comment -- nothing to release before deleting.
	}
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DEFAULT.WAV");

	// The contrast for this layer: the SAME file, the SAME active stub, calling
	// getAudioFileFromFilename directly again, differing only in mayLoadAsync being explicit true
	// instead of omitted. This MUST reach the loader and report LOAD_IN_PROGRESS.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		std::string path = "SAMPLES/DEFAULT.WAV";
		Error error = Error::NONE;
		AudioFile* file =
		    audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                              /*makeWaveTableWorkAtAllCosts=*/false,
		                                              /*mayLoadAsync=*/true);
		if (file != nullptr || error != Error::LOAD_IN_PROGRESS) {
			printf("FAIL [loadFileDefault]: getAudioFileFromFilename(..., mayLoadAsync=true) did not "
			       "reach the async path with the loader active (file=%p error=%d) -- the "
			       "default-vs-explicit contrast this check exists to see is broken\n",
			       static_cast<void*>(file), static_cast<int>(error));
			ok = false;
		}
		if (g_asyncStub.requestCalls != 1) {
			printf("FAIL [loadFileDefault]: expected exactly one async request for the explicit "
			       "mayLoadAsync=true call (manager layer), got %d\n",
			       g_asyncStub.requestCalls);
			ok = false;
		}
	}

	g_asyncStub = {};
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DEFAULT.WAV");
	if (hadPreviousRoot) {
		setenv("DELUGE_SD_ROOT", previousRoot.c_str(), 1);
	}
	else {
		unsetenv("DELUGE_SD_ROOT");
	}
	removeTempCard();
	return ok;
}

/// Pin that a whole LOAD resolves to the same files whichever path serves it.
///
/// This is the case an A/B of the two paths cannot catch on its own: a sample that exists in only one
/// place is found there either way, so the two agree for the wrong reason. Here they can disagree, and
/// the difference is which file the user hears.
///
/// It runs a load, not a sample, because `alternateLoadDirStatus` is per-LOAD state that both paths
/// share and that each load MUTATES as it goes: the synchronous path promotes it only after a regular
/// open misses, so a collector that promotes it earlier changes the try order for every later sample
/// of that load -- including samples that never go near the collector. One sample per leg cannot see
/// that; three, in one load, can.
///
/// The card holds the same filename twice (regular and alternate, different lengths and contents)
/// twice over, plus one sample that exists ONLY in the alternate dir -- which is what the eager probe
/// existed to find, and which must still be found now that the probe happens lazily.
bool checkAsyncAlternateDirOrder() {
	bool ok = true;

	char rootTemplate[] = "/tmp/deluge_loadcheck_alt_XXXXXX";
	const char* root = mkdtemp(rootTemplate);
	if (root == nullptr) {
		printf("FAIL [asyncAltDirOrder]: could not create a temp card directory\n");
		return false;
	}
	const std::string samplesDir = std::string{root} + "/SAMPLES";
	const std::string altDir = samplesDir + "/ALT";
	mkdir(samplesDir.c_str(), 0755);
	mkdir(altDir.c_str(), 0755);

	const auto removeTempCard = [&] {
		for (const char* name : {"/DUP.WAV", "/DUP2.WAV", "/BAD.WAV", "/ALT_ONLY.WAV"}) {
			unlink((samplesDir + name).c_str());
			unlink((altDir + name).c_str());
		}
		rmdir(altDir.c_str());
		rmdir(samplesDir.c_str());
		rmdir(root);
	};

	// Different lengths AND different fills, so a mix-up shows up in the geometry and in the bytes.
	const bool fixtureWritten =
	    writeTestWav(samplesDir + "/DUP.WAV", 2000, 0x11) && writeTestWav(altDir + "/DUP.WAV", 3000, 0x22)
	    && writeTestWav(samplesDir + "/DUP2.WAV", 2400, 0x33) && writeTestWav(altDir + "/DUP2.WAV", 3400, 0x44)
	    && writeTestWav(altDir + "/ALT_ONLY.WAV", 2800, 0x55)
	    && writeTestWav(altDir + "/BAD.WAV", 3600, 0x66)
	    // BAD.WAV's regular copy is not audio at all: it OPENS and then fails to parse, which is a
	    // verdict about that file rather than a reason to go looking in the alternate dir -- where a
	    // perfectly good file is sitting, ready to be loaded silently in its place.
	    && writeGarbageFile(samplesDir + "/BAD.WAV", 2600);
	if (!fixtureWritten) {
		printf("FAIL [asyncAltDirOrder]: could not write the temp card's fixture files\n");
		removeTempCard();
		return false;
	}

	const char* previousRootRaw = getenv("DELUGE_SD_ROOT");
	const std::string previousRoot = (previousRootRaw != nullptr) ? previousRootRaw : "";
	const bool hadPreviousRoot = previousRootRaw != nullptr;
	setenv("DELUGE_SD_ROOT", root, 1);

	// The load, in order. BAD.WAV sits before ALT_ONLY.WAV deliberately: a wrongful retry on its parse
	// verdict would promote the alternate-dir status, and every sample after it would inherit that.
	static const char* const kFixtureNames[] = {"SAMPLES/DUP.WAV", "SAMPLES/DUP2.WAV", "SAMPLES/BAD.WAV",
	                                            "SAMPLES/ALT_ONLY.WAV"};

	// What a leg loaded, in the terms that decide which file plays.
	struct LoadedIdentity {
		bool loaded = false;
		std::string filePath;
		std::string loadedFromAlternatePath;
		uint64_t audioDataLengthBytes = 0;
		std::array<std::byte, 16> firstBytes{};
		bool readOk = false;
		Error error = Error::NONE;
		int ticks = 0; ///< how many calls it took to be answered
	};

	// One load of three samples, either synchronously or through the collector. The async leg asks
	// again on LOAD_IN_PROGRESS exactly as its caller is required to, rather than assuming one tick.
	const auto runLoad = [&](bool useAsync) {
		g_asyncStub = {};
		if (useAsync) {
			g_asyncStub.active = true;
			g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
			g_asyncStub.resolveAgainstRoot = root;
		}
		// A song load: the alternate dir might exist, and the in-memory alternate-name search is
		// skipped for SONG, so this is purely about which candidate is opened first.
		for (const char* name : kFixtureNames) {
			audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(name);
			// A leg that started with one of these still resident would be served from memory and
			// agree with the other leg without going near the loader at all -- every comparison below
			// would then pass for the wrong reason.
			bool stillResident = false;
			(void)audioFileManager.audioFiles.search(name, &stillResident);
			if (stillResident) {
				printf("FAIL [asyncAltDirOrder]: %s survived into the next load; the leg would be served "
				       "from memory\n",
				       name);
				ok = false;
			}
		}
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
		audioFileManager.thingTypeBeingLoaded = ThingType::SONG;

		std::vector<LoadedIdentity> loaded;
		for (const char* name : kFixtureNames) {
			LoadedIdentity identity;
			std::string path = name;
			AudioFile* file = nullptr;
			// Bounded: one request, at most one alternate-dir retry, and the collect that answers it.
			for (int tick = 0; tick < 4; tick++) {
				identity.error = Error::NONE;
				identity.ticks = tick + 1;
				file = audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &identity.error,
				                                                 AudioFileType::SAMPLE,
				                                                 /*makeWaveTableWorkAtAllCosts=*/false,
				                                                 /*mayLoadAsync=*/useAsync);
				if (identity.error != Error::LOAD_IN_PROGRESS) {
					break;
				}
			}
			if (file != nullptr && file->type == AudioFileType::SAMPLE) {
				Sample& sample = static_cast<Sample&>(*file);
				identity.loaded = true;
				identity.filePath = sample.filePath;
				identity.loadedFromAlternatePath = sample.loadedFromAlternatePath;
				identity.audioDataLengthBytes = sample.audioDataLengthBytes;
				auto readSource = sample.stream().make_read_source();
				identity.readOk = readSource != nullptr && readSource->read(0, identity.firstBytes).has_value();
			}
			loaded.push_back(identity);
		}
		return loaded;
	};

	const std::vector<LoadedIdentity> synchronous = runLoad(/*useAsync=*/false);
	const AlternateLoadDirStatus statusAfterSync = audioFileManager.alternateLoadDirStatus;
	const int syncRequests = g_asyncStub.requestCalls;
	const int syncCollects = g_asyncStub.collectCalls;
	const std::vector<LoadedIdentity> asynchronous = runLoad(/*useAsync=*/true);
	const AlternateLoadDirStatus statusAfterAsync = audioFileManager.alternateLoadDirStatus;
	const int asyncRequests = g_asyncStub.requestCalls;
	const int asyncCollects = g_asyncStub.collectCalls;

	// Each leg has to have been served by the path it claims, or every comparison below is between two
	// runs of the same one.
	if (syncRequests != 0 || syncCollects != 0) {
		printf("FAIL [asyncAltDirOrder]: the synchronous leg reached the loader (%d request(s), %d collect(s))\n",
		       syncRequests, syncCollects);
		ok = false;
	}
	if (asyncRequests < static_cast<int>(std::size(kFixtureNames))
	    || asyncCollects < static_cast<int>(std::size(kFixtureNames))) {
		printf("FAIL [asyncAltDirOrder]: the async leg did not go through the loader for every sample "
		       "(%d request(s), %d collect(s) for %zu samples)\n",
		       asyncRequests, asyncCollects, std::size(kFixtureNames));
		ok = false;
	}

	for (size_t i = 0; i < synchronous.size(); i++) {
		const LoadedIdentity& sync = synchronous[i];
		const LoadedIdentity& async = asynchronous[i];
		if (sync.loaded != async.loaded || sync.error != async.error) {
			printf("FAIL [asyncAltDirOrder]: the two paths disagree about %s (loaded %d/%d, errors %d/%d, "
			       "%d/%d tick(s))\n",
			       kFixtureNames[i], static_cast<int>(sync.loaded), static_cast<int>(async.loaded),
			       static_cast<int>(sync.error), static_cast<int>(async.error), sync.ticks, async.ticks);
			ok = false;
			continue;
		}
		if (!sync.loaded) {
			continue; // both refused it, with the same error: nothing further to compare
		}
		// A sample the collector answered must have taken at least two calls -- one cannot both
		// request and collect -- so a leg quietly served from memory shows up here.
		if (async.ticks < 2) {
			printf("FAIL [asyncAltDirOrder]: %s was answered in one call (%d tick(s)); it cannot have been "
			       "requested AND collected\n",
			       kFixtureNames[i], async.ticks);
			ok = false;
		}
		if (sync.filePath != async.filePath || sync.loadedFromAlternatePath != async.loadedFromAlternatePath
		    || sync.audioDataLengthBytes != async.audioDataLengthBytes) {
			printf("FAIL [asyncAltDirOrder]: the two paths loaded DIFFERENT files for \"%s\" -- sync {path=\"%s\" "
			       "alt=\"%s\" dataLen=%llu} vs async {path=\"%s\" alt=\"%s\" dataLen=%llu}\n",
			       kFixtureNames[i], sync.filePath.c_str(), sync.loadedFromAlternatePath.c_str(),
			       static_cast<unsigned long long>(sync.audioDataLengthBytes), async.filePath.c_str(),
			       async.loadedFromAlternatePath.c_str(), static_cast<unsigned long long>(async.audioDataLengthBytes));
			ok = false;
		}
		if (!sync.readOk || !async.readOk || sync.firstBytes != async.firstBytes) {
			printf("FAIL [asyncAltDirOrder]: the two paths' streams read different bytes for \"%s\" (readable "
			       "%d/%d) -- the bound file is not the same one\n",
			       kFixtureNames[i], static_cast<int>(sync.readOk), static_cast<int>(async.readOk));
			ok = false;
		}
	}

	// The sample that exists ONLY in the alternate dir must still be found there -- the requirement the
	// up-front probe used to serve, now met by resolving after every proposed candidate has missed.
	if (asynchronous.size() == std::size(kFixtureNames)
	    && (!asynchronous[3].loaded || asynchronous[3].loadedFromAlternatePath != "SAMPLES/ALT/ALT_ONLY.WAV")) {
		printf("FAIL [asyncAltDirOrder]: the alternate-dir-only sample was not loaded from the alternate dir "
		       "(loaded=%d alt=\"%s\")\n",
		       static_cast<int>(asynchronous[3].loaded), asynchronous[3].loadedFromAlternatePath.c_str());
		ok = false;
	}

	// A file that opened and failed to parse must be REPORTED, not worked around. Retrying on a parse
	// verdict would find the good copy in the alternate dir and load THAT -- a different file, reported
	// as success, where the synchronous path reports the parse error.
	if (asynchronous.size() == std::size(kFixtureNames)
	    && (asynchronous[2].loaded || asynchronous[2].error != Error::FILE_UNSUPPORTED)) {
		printf("FAIL [asyncAltDirOrder]: a file that opened and failed to parse was not reported as that "
		       "(loaded=%d error=%d) -- a parse verdict must not send the load to the alternate dir\n",
		       static_cast<int>(asynchronous[2].loaded), static_cast<int>(asynchronous[2].error));
		ok = false;
	}

	// The load's shared state must evolve the same way too, or the NEXT sample -- including one taking
	// the synchronous path -- inherits a different try order from whichever path happened to run first.
	if (statusAfterSync != statusAfterAsync) {
		printf("FAIL [asyncAltDirOrder]: the alternate-dir status ended the load differently (sync=%d async=%d) "
		       "-- the collector is promoting it somewhere the synchronous path would not\n",
		       static_cast<int>(statusAfterSync), static_cast<int>(statusAfterAsync));
		ok = false;
	}

	// And it must not be promoted at all by a load whose samples are all where they say they are: the
	// synchronous path probes only after a regular open MISSES, so promoting earlier would flip the
	// try order for every later sample of the load.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
		g_asyncStub.resolveAgainstRoot = root;
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DUP.WAV");
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
		std::string path = "SAMPLES/DUP.WAV";
		Error error = Error::NONE;
		for (int tick = 0; tick < 4; tick++) {
			error = Error::NONE;
			(void)audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
			                                                /*makeWaveTableWorkAtAllCosts=*/false,
			                                                /*mayLoadAsync=*/true);
			if (error != Error::LOAD_IN_PROGRESS) {
				break;
			}
		}
		if (audioFileManager.alternateLoadDirStatus != AlternateLoadDirStatus::MIGHT_EXIST) {
			printf("FAIL [asyncAltDirOrder]: a sample found at its regular path promoted the alternate-dir "
			       "status (to %d) -- that leaks a changed try order into the rest of the load\n",
			       static_cast<int>(audioFileManager.alternateLoadDirStatus));
			ok = false;
		}
		const std::vector<std::string> expected{"SAMPLES/DUP.WAV"};
		if (g_asyncStub.requestedCandidates != expected) {
			printf("FAIL [asyncAltDirOrder]: an unresolved alternate dir should propose the regular path alone, "
			       "got %zu candidate(s)\n",
			       g_asyncStub.requestedCandidates.size());
			ok = false;
		}
	}

	g_asyncStub = {};
	for (const char* name : kFixtureNames) {
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(name);
	}
	audioFileManager.alternateAudioFileLoadPath.clear();
	audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	audioFileManager.thingTypeBeingLoaded = ThingType::NONE;
	removeTempCard();
	if (hadPreviousRoot) {
		setenv("DELUGE_SD_ROOT", previousRoot.c_str(), 1);
	}
	else {
		unsetenv("DELUGE_SD_ROOT");
	}
	return ok;
}

/// Pin the admission rule against the corpus the Rust side checks itself with.
///
/// The rule decides which paths ever become an async request; a LOOSER rule here than the loader's is
/// a load that is asked for forever and never completes. Both sides read the same file, so a drift
/// fails a test rather than resurfacing as silence -- see path_admission_corpus.inc.
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

void deluge_loadcheck_driver() {
	static bool started = false;
	if (started) {
		return;
	}
	started = true;

	int failures = 0;
	if (!checkProposeLoadCandidates()) {
		failures++;
	}
	if (!checkAudioLoadAbiAbandon()) {
		failures++;
	}
	if (!checkAsyncCollector()) {
		failures++;
	}
	if (!checkAsyncAlternateDirOrder()) {
		failures++;
	}
	if (!checkPathAdmissionCorpus()) {
		failures++;
	}
	for (const LoadItem& item : g_items) {
		std::string path = item.path;
		Error error = Error::NONE;
		AudioFile* audioFile = audioFileManager.getAudioFileFromFilename(
		    path, /*mayReadCard=*/true, &error, item.wavetable ? AudioFileType::WAVETABLE : AudioFileType::SAMPLE,
		    /*makeWaveTableWorkAtAllCosts=*/false);

		if (audioFile == nullptr) {
			printf("FAILED %s | error=%s (%d)\n", item.path.c_str(), errorName(error), static_cast<int>(error));
			failures++;
			continue;
		}
		if (item.wavetable) {
			dumpWaveTable(item.path.c_str(), static_cast<WaveTable&>(*audioFile));
		}
		else {
			dumpSample(item.path.c_str(), static_cast<Sample&>(*audioFile));
		}

		if (!checkLoadInvariants(item.path.c_str())) {
			failures++;
		}
	}

	fflush(nullptr);
	quick_exit(failures == 0 ? 0 : 1);
}

void usage(const char* argv0) {
	fprintf(stderr, "usage: %s [--project <dir> (--file PATH | --wavetable PATH)...]\n", argv0);
	fprintf(stderr, "  With no --file/--wavetable at all, runs only the fixture-free checks\n");
	fprintf(stderr, "  (checkProposeLoadCandidates, checkAudioLoadAbiAbandon, checkAsyncCollector,\n");
	fprintf(stderr, "  checkLoadFileDefaultStaysSynchronous, checkAsyncAlternateDirOrder,\n");
	fprintf(stderr, "  checkPathAdmissionCorpus) and\n");
	fprintf(stderr, "  exits -- no app boot, no card fixture, --project ignored. This is what the harness\n");
	fprintf(stderr, "  registry runs as a gate.\n");
	fprintf(stderr, "  With --file/--wavetable, --project <dir> is required.\n");
}

} // namespace

int main(int argc, char** argv) {
	const char* project = nullptr;

	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		const char* v = (i + 1 < argc) ? argv[i + 1] : nullptr;
		if (strcmp(a, "--project") == 0 && v) {
			project = v;
			i++;
		}
		else if (strcmp(a, "--file") == 0 && v) {
			g_items.push_back({v, false});
			i++;
		}
		else if (strcmp(a, "--wavetable") == 0 && v) {
			g_items.push_back({v, true});
			i++;
		}
		else {
			usage(argv[0]);
			return 1;
		}
	}

	if (g_items.empty()) {
		// Pure policy checks only: no card, no filesystem, no app boot. Deliberately does NOT call
		// deluge_platform_init()/deluge_main() here, even with --project defaulted to something benign --
		// booting the app has real side effects (RuntimeFeatureSettings::readSettingsFromFile()
		// unconditionally mkdir's SETTINGS/ under whatever DELUGE_SD_ROOT resolves to), and this mode's
		// whole point is to be safe to run from anywhere -- including the repo root, with zero arguments,
		// which is exactly what the harness registry entry does -- with no fixture and no cleanup
		// required. --project is unused and ignored here.
		const bool policyOk = checkProposeLoadCandidates();
		const bool abiOk = checkAudioLoadAbiAbandon();
		// Serves itself a two-file temp directory as the card (removed again before it returns), so it
		// needs no fixture here either -- see its own doc comment.
		const bool collectorOk = checkAsyncCollector();
		const bool loadFileDefaultOk = checkLoadFileDefaultStaysSynchronous();
		const bool altDirOrderOk = checkAsyncAlternateDirOrder();
		const bool admissionOk = checkPathAdmissionCorpus();
		fflush(nullptr);
		return (policyOk && abiOk && collectorOk && loadFileDefaultOk && altDirOrderOk && admissionOk) ? 0 : 1;
	}

	if (project == nullptr) {
		usage(argv[0]);
		return 1;
	}

	// The project directory IS the SD card: host_efatfs_passthrough.cpp serves every task-context
	// file operation (including the streaming-read path, which is efatfs-only) straight from it.
	setenv("DELUGE_SD_ROOT", project, 1);

	if (getenv("DELUGE_HOST_DETERMINISTIC") == nullptr) {
		setenv("DELUGE_HOST_DETERMINISTIC", "1", 1);
	}
	if (getenv("DELUGE_HOST_AUDIO") == nullptr) {
		setenv("DELUGE_HOST_AUDIO", "off", 1);
	}

	startupConditionalTask = deluge_loadcheck_driver; // override before deluge_main registers it

	deluge_platform_init();
	deluge_main(); // never returns; deluge_loadcheck_driver quick_exit()s

	return 0;
}
