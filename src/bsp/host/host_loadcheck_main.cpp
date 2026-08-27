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
/// audio_load.h ownership check and checkAsyncCollector()'s async-load miss path -- it does not call
/// deluge_platform_init() or deluge_main(), so there is no app boot, no card fixture, and --project is
/// ignored (each check self-initializes the allocator/resource-manager it touches, and the collector
/// check serves itself a temp directory as its card and removes it again). This is
/// what the harness registry entry for this binary runs as a standing gate (harness/registry.toml);
/// running it with zero arguments from any directory, including the repo root, leaves the tree
/// untouched.

#include "libdeluge/audio_load.h"
#include "memory/general_memory_allocator.h" // GeneralMemoryAllocator::resourceManager()
#include "model/sample/sample.h"
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

/// Assert the two invariants an async load must preserve (design §4b, §6): the manager holds no
/// half-built object (adopted, reasoned, but with zero geometry), and no load leaked a reason.
/// Both hold trivially on the synchronous path — pinning them here makes a later async break loud.
///
/// The geometry half only applies to Samples (a WaveTable has no byteDepth/numChannels concept of
/// its own in this sense). The leaked-reason half applies to every AudioFile: this driver never
/// calls addReason() itself, and buildAudioFileFromCard's own protect-during-setup reason is always
/// dropped before getAudioFileFromFilename returns (removeReason("E399") — "Setup done ... the
/// caller re-leases"), so every object reachable here should sit at leaseCount() == 0 between items.
bool checkLoadInvariants(const char* label) {
	bool ok = true;
	for (int32_t i = 0; i < static_cast<int32_t>(audioFileManager.audioFiles.size()); i++) {
		AudioFile* f = audioFileManager.audioFiles[i];

		if (f->type == AudioFileType::SAMPLE) {
			Sample* s = static_cast<Sample*>(f);
			if (s->byteDepth == 0 || s->numChannels == 0) {
				printf("FAIL [%s]: zero-geometry Sample in cache: %s\n", label, s->filePath.c_str());
				ok = false;
			}
		}

		if (f->leaseCount() != 0) {
			printf("FAIL [%s]: leaked reason(s) on %s (leaseCount=%u)\n", label, f->filePath.c_str(), f->leaseCount());
			ok = false;
		}
	}
	return ok;
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
	g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_READY;
}

} // namespace

extern "C" bool deluge_audio_load_async_active() noexcept {
	return g_asyncStub.active;
}

extern "C" uint8_t deluge_audio_load_collect(const char* file_path, DelugeAudioLoadResult* out) noexcept {
	(void)file_path;
	g_asyncStub.collectCalls++;
	if (g_asyncStub.collectStatus == DELUGE_AUDIO_LOAD_READY) {
		*out = g_asyncStub.result;
	}
	return g_asyncStub.collectStatus;
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

	// UNKNOWN: the loader has never heard of this path, so ask it. Two things about the request are
	// pinned here, both driven from MIGHT_EXIST -- the state every song and preset load starts in:
	//   * the alternate load dir is resolved BEFORE the candidates are proposed, or a "collect media"
	//     song's samples are requested with only their regular path (proposeLoadCandidates yields one
	//     candidate in that state and three once the probe has promoted it);
	//   * and the list is then rotated so the REGULAR path is tried first, which is what resolveFileSize
	//     effectively does from the same state -- see checkAsyncAlternateDirOrder for what the two
	//     orders cost when a file exists at both locations.
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
		const std::vector<std::string> expected{"SAMPLES/FOLDER/kick.wav", "SAMPLES/ALT/FOLDER_kick.wav",
		                                        "SAMPLES/ALT/kick.wav"};
		if (g_asyncStub.requestedCandidates != expected) {
			printf("FAIL [asyncCollector]: the request's candidates are not the resolved-alternate-dir list, "
			       "regular path first (got %zu:",
			       g_asyncStub.requestedCandidates.size());
			for (const std::string& c : g_asyncStub.requestedCandidates) {
				printf(" \"%s\"", c.c_str());
			}
			printf(") -- the alternate dir must be resolved before candidates are proposed, and the "
			       "regular path must lead\n");
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

/// Pin that both paths load the SAME FILE when one exists at both its regular path and in the
/// alternate load dir.
///
/// This is the case an A/B of the two paths cannot catch on its own: a sample that exists in only one
/// place is found there either way, so the two agree for the wrong reason. Here they can disagree, and
/// the difference is which file the user hears.
///
/// The synchronous path's effective try order is not proposeLoadCandidates' order. From `MIGHT_EXIST`
/// -- the state every song and preset load starts in -- resolveFileSize proposes only the REGULAR path,
/// tries it first, and probes the alternate dir only once that misses. The collector cannot probe
/// lazily (its one request carries every candidate), so it resolves up front and then has to put the
/// order back.
///
/// Both legs run from a fresh `MIGHT_EXIST` against a card holding the same filename twice with
/// different lengths and contents, and the resulting Samples must agree on identity, geometry and the
/// bytes read back through the stream.
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
	const std::string regularFile = samplesDir + "/DUP.WAV";
	const std::string alternateFile = altDir + "/DUP.WAV";
	mkdir(samplesDir.c_str(), 0755);
	mkdir(altDir.c_str(), 0755);
	// Different lengths AND different contents, so a mix-up shows up in the geometry and in the bytes.
	const auto removeTempCard = [&] {
		unlink(regularFile.c_str());
		unlink(alternateFile.c_str());
		rmdir(altDir.c_str());
		rmdir(samplesDir.c_str());
		rmdir(root);
	};
	const bool fixtureWritten = writeTestWav(regularFile, 2000, 0x11) && writeTestWav(alternateFile, 3000, 0x22);
	if (!fixtureWritten) {
		printf("FAIL [asyncAltDirOrder]: could not write the temp card's duplicate sample files\n");
		removeTempCard();
		return false;
	}

	const char* previousRootRaw = getenv("DELUGE_SD_ROOT");
	const std::string previousRoot = (previousRootRaw != nullptr) ? previousRootRaw : "";
	const bool hadPreviousRoot = previousRootRaw != nullptr;
	setenv("DELUGE_SD_ROOT", root, 1);

	// What each leg loaded, in the terms that decide which file plays.
	struct LoadedIdentity {
		bool loaded = false;
		std::string filePath;
		std::string loadedFromAlternatePath;
		uint64_t audioDataLengthBytes = 0;
		std::array<std::byte, 16> firstBytes{};
		bool readOk = false;
	};

	const auto capture = [](AudioFile* file) {
		LoadedIdentity identity;
		if (file == nullptr || file->type != AudioFileType::SAMPLE) {
			return identity;
		}
		Sample& sample = static_cast<Sample&>(*file);
		identity.loaded = true;
		identity.filePath = sample.filePath;
		identity.loadedFromAlternatePath = sample.loadedFromAlternatePath;
		identity.audioDataLengthBytes = sample.audioDataLengthBytes;
		auto readSource = sample.stream().make_read_source();
		identity.readOk = readSource != nullptr && readSource->read(0, identity.firstBytes).has_value();
		return identity;
	};

	// A load of a song's sample: the alternate dir might exist, and the in-memory alternate-name search
	// is skipped for SONG, so this is purely about which candidate is opened first.
	const auto startOfLoad = [&]() {
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DUP.WAV");
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
		audioFileManager.thingTypeBeingLoaded = ThingType::SONG;
	};

	g_asyncStub = {};
	startOfLoad();
	std::string syncPath = "SAMPLES/DUP.WAV";
	Error syncError = Error::NONE;
	const LoadedIdentity synchronous = capture(
	    audioFileManager.getAudioFileFromFilename(syncPath, /*mayReadCard=*/true, &syncError, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/false));

	// Now the same load through the collector, with the stub resolving the candidates it is given
	// against the same card -- so the ORDER of that list is what decides which file wins.
	g_asyncStub = {};
	g_asyncStub.active = true;
	g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
	g_asyncStub.resolveAgainstRoot = root;
	startOfLoad();
	std::string asyncPath = "SAMPLES/DUP.WAV";
	Error asyncError = Error::NONE;
	AudioFile* pending =
	    audioFileManager.getAudioFileFromFilename(asyncPath, /*mayReadCard=*/true, &asyncError, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false, /*mayLoadAsync=*/true);
	if (pending != nullptr || asyncError != Error::LOAD_IN_PROGRESS) {
		printf("FAIL [asyncAltDirOrder]: the first tick should request and report LOAD_IN_PROGRESS (file=%p "
		       "error=%d)\n",
		       static_cast<void*>(pending), static_cast<int>(asyncError));
		ok = false;
	}
	asyncError = Error::NONE;
	const LoadedIdentity asynchronous = capture(
	    audioFileManager.getAudioFileFromFilename(asyncPath, /*mayReadCard=*/true, &asyncError, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/true));

	if (!synchronous.loaded || !asynchronous.loaded) {
		printf("FAIL [asyncAltDirOrder]: a leg did not load a Sample at all (sync=%d async=%d, errors %d/%d)\n",
		       static_cast<int>(synchronous.loaded), static_cast<int>(asynchronous.loaded), static_cast<int>(syncError),
		       static_cast<int>(asyncError));
		ok = false;
	}
	else {
		if (synchronous.filePath != asynchronous.filePath
		    || synchronous.loadedFromAlternatePath != asynchronous.loadedFromAlternatePath
		    || synchronous.audioDataLengthBytes != asynchronous.audioDataLengthBytes) {
			printf("FAIL [asyncAltDirOrder]: the two paths loaded DIFFERENT files for \"SAMPLES/DUP.WAV\" -- "
			       "sync {path=\"%s\" alt=\"%s\" dataLen=%llu} vs async {path=\"%s\" alt=\"%s\" dataLen=%llu}\n",
			       synchronous.filePath.c_str(), synchronous.loadedFromAlternatePath.c_str(),
			       static_cast<unsigned long long>(synchronous.audioDataLengthBytes), asynchronous.filePath.c_str(),
			       asynchronous.loadedFromAlternatePath.c_str(),
			       static_cast<unsigned long long>(asynchronous.audioDataLengthBytes));
			ok = false;
		}
		if (!synchronous.readOk || !asynchronous.readOk || synchronous.firstBytes != asynchronous.firstBytes) {
			printf("FAIL [asyncAltDirOrder]: the two paths' streams read different bytes (readable %d/%d) -- the "
			       "bound file is not the same one\n",
			       static_cast<int>(synchronous.readOk), static_cast<int>(asynchronous.readOk));
			ok = false;
		}
	}

	// The accepted residual of resolving up front: an alternate dir that is genuinely absent is
	// recorded as NOT_FOUND right away, where the synchronous path would have left the status alone
	// until a regular open missed. The only consequence is that getAudioFileFromFilename's in-memory
	// alternate-name search stops being tried for the rest of the load -- a cache lookup that could
	// only have hit on an object put there by an earlier load through the same (now absent) directory.
	// Accepted rather than restored, because re-arming MIGHT_EXIST would mean probing the directory
	// again on every single request. Asserted so the choice cannot drift unnoticed.
	{
		g_asyncStub = {};
		g_asyncStub.active = true;
		g_asyncStub.collectStatus = DELUGE_AUDIO_LOAD_UNKNOWN;
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/NO_SUCH_ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
		std::string path = "SAMPLES/DUP.WAV";
		Error error = Error::NONE;
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DUP.WAV");
		(void)audioFileManager.getAudioFileFromFilename(path, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
		                                                /*makeWaveTableWorkAtAllCosts=*/false, /*mayLoadAsync=*/true);
		if (audioFileManager.alternateLoadDirStatus != AlternateLoadDirStatus::NOT_FOUND) {
			printf("FAIL [asyncAltDirOrder]: an absent alternate dir should be resolved to NOT_FOUND by the "
			       "request (status=%d) -- if that changed deliberately, change this assertion with it\n",
			       static_cast<int>(audioFileManager.alternateLoadDirStatus));
			ok = false;
		}
		// The candidate list for an absent alternate dir is just the regular path, so there is nothing
		// to rotate and the request must carry exactly one candidate.
		const std::vector<std::string> expected{"SAMPLES/DUP.WAV"};
		if (g_asyncStub.requestedCandidates != expected) {
			printf("FAIL [asyncAltDirOrder]: an absent alternate dir should leave one candidate, got %zu\n",
			       g_asyncStub.requestedCandidates.size());
			ok = false;
		}
	}

	g_asyncStub = {};
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists("SAMPLES/DUP.WAV");
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
	fprintf(stderr, "  (checkProposeLoadCandidates, checkAudioLoadAbiAbandon, checkAsyncCollector) and\n");
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
		const bool altDirOrderOk = checkAsyncAlternateDirOrder();
		fflush(nullptr);
		return (policyOk && abiOk && collectorOk && altDirOrderOk) ? 0 : 1;
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
