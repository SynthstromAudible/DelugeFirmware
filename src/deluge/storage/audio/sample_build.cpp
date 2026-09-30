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

/// The card-sample build (sample_build.h): thin adapters over AudioFileManager's no-I/O
/// prologue/epilogue (beginSampleFromCard/finishSampleFromCard), the I/O-free candidate policy
/// (proposeLoadCandidates), the one I/O probe the async loader must sequence before it
/// (resolveAlternateLoadDirStatus), and the streaming read-handle bind (SampleStream::open_read_stream)
/// a published build must have. Also the numeric `Error` mapping for the byte libdeluge/audio_load.h
/// carries a loader verdict in (audio_load_abi.h).
///
/// Every build function is `noexcept`, and each catches with `catch (...)` so that is honest: the
/// repo's allocators throw `deluge::exception::BAD_ALLOC` (operators.cpp), and anything escaping a
/// `noexcept` frame is `std::terminate()` -- on this target an unexplained, silent device death. Where
/// a catch handler itself calls back into C++ (begin()'s cleanup path), that call is wrapped in its own
/// nested try/catch so a second exception can't escape either.

#include "storage/audio/sample_build.h"

#include "definitions_cxx.hpp" // kFilenameBufferSize
#include "model/sample/sample.h"
#include "storage/audio/audio_file_format.h" // RawDataFormat
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/audio_load_abi.h"
#include "storage/audio/audio_load_context.h"
#include "util/misc.h" // util::to_underlying

#include "libdeluge/audio_load.h"

#include <algorithm>
#include <cstddef> // offsetof
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

static_assert(DELUGE_MAX_PATH == kFilenameBufferSize,
              "DELUGE_MAX_PATH documents itself as matching this codebase's existing whole-path ceiling");

namespace deluge::storage::sample_build {

/// A build in flight: the Sample plus the sizeBytes finishSampleFromCard needs for finalizeAfterLoad
/// (buildAudioFileFromCard's caller resolves this once and threads it through both halves; a caller
/// of this split build has to carry it the same way, since Sample itself has nowhere to park it before
/// finalizeAfterLoad runs). `magic` is consumed-tracking only -- see validateBuild().
struct SampleBuild {
	/// Marks a build as live. Arbitrary but stable -- not a format tag, just a canary.
	static constexpr uint32_t kLive = 0x4C445342u; // "LDSB"
	/// Marks a build as consumed (finished or abandoned already), written just before it is freed.
	static constexpr uint32_t kConsumed = 0u;

	uint32_t magic = kLive;
	Sample* sample;
	uint64_t sizeBytes;
};

} // namespace deluge::storage::sample_build

namespace {

using deluge::storage::sample_build::SampleBuild;

/// Validate a build before touching it. Null and an already-consumed build both come back as "not a
/// live build" instead of being dereferenced further -- the header promises `finish`/`abandon`/
/// `applyGeometry`/`bindReadStream` are safe against exactly these.
///
/// Reading `build->magic` on a genuinely freed pointer is still, strictly, undefined behaviour -- there
/// is no way to validate an arbitrary pointer without a registry this build doesn't keep, and a
/// sanitizer build may well report the read itself as a heap-use-after-free (which is a perfectly good
/// "fails loudly" outcome). What this DOES reliably catch, on a build without a sanitizer attached, is
/// the realistic caller bug -- reusing the same build across two finish/abandon calls -- turning it
/// into a clean refusal instead of dispatching straight through to a Sample that has already been
/// destructed.
SampleBuild* validateBuild(SampleBuild* build) noexcept {
	if (build == nullptr || build->magic != SampleBuild::kLive) {
		return nullptr;
	}
	return build;
}

/// Write @p value to @p out when the caller asked for it.
void report(Error* out, Error value) noexcept {
	if (out != nullptr) {
		*out = value;
	}
}

// Every Error code crossing libdeluge/audio_load.h (DelugeAudioLoadResult::error, which the Rust
// loader writes from its own copies of these numbers) is matched by NUMBER (errorFromAbiByte below), never
// `static_cast<Error>(byte)` -- `enum class Error` has no fixed underlying type, so its valid range is whatever `int`
// gives it and casting an out-of-range byte is undefined behaviour. These static_asserts pin the numeric identity
// errorFromAbiByte depends on: inserting a new Error enumerator upstream (definitions_cxx.hpp) shifts every later
// value, and without this, that shift would be silent -- every gate staying green while a caller across this boundary
// mismatches every code from the insertion point on.
static_assert(util::to_underlying(Error::NONE) == 0);
static_assert(util::to_underlying(Error::INSUFFICIENT_RAM) == 1);
static_assert(util::to_underlying(Error::UNSPECIFIED) == 2);
static_assert(util::to_underlying(Error::SD_CARD) == 3);
static_assert(util::to_underlying(Error::NO_FURTHER_PRESETS) == 4);
static_assert(util::to_underlying(Error::FILE_CORRUPTED) == 5);
static_assert(util::to_underlying(Error::FILE_UNREADABLE) == 6);
static_assert(util::to_underlying(Error::FILE_UNSUPPORTED) == 7);
static_assert(util::to_underlying(Error::FILE_FIRMWARE_VERSION_TOO_NEW) == 8);
static_assert(util::to_underlying(Error::RESULT_TAG_UNUSED) == 9);
static_assert(util::to_underlying(Error::FOLDER_DOESNT_EXIST) == 10);
static_assert(util::to_underlying(Error::WRITE_PROTECTED) == 11);
static_assert(util::to_underlying(Error::BUG) == 12);
static_assert(util::to_underlying(Error::WRITE_FAIL) == 13);
static_assert(util::to_underlying(Error::FILE_TOO_BIG) == 14);
static_assert(util::to_underlying(Error::PRESET_IN_USE) == 15);
static_assert(util::to_underlying(Error::NO_FURTHER_FILES_THIS_DIRECTION) == 16);
static_assert(util::to_underlying(Error::FILE_ALREADY_EXISTS) == 17);
static_assert(util::to_underlying(Error::FILE_NOT_FOUND) == 18);
static_assert(util::to_underlying(Error::ABORTED_BY_USER) == 19);
static_assert(util::to_underlying(Error::MAX_FILE_SIZE_REACHED) == 20);
static_assert(util::to_underlying(Error::SD_CARD_FULL) == 21);
static_assert(util::to_underlying(Error::FILE_NOT_LOADABLE_AS_WAVETABLE) == 22);
static_assert(util::to_underlying(Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO) == 23);
static_assert(util::to_underlying(Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP) == 24);
static_assert(util::to_underlying(Error::NO_ERROR_BUT_GET_OUT) == 25);
static_assert(util::to_underlying(Error::INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE) == 26);
static_assert(util::to_underlying(Error::SD_CARD_NOT_PRESENT) == 27);
static_assert(util::to_underlying(Error::SD_CARD_NO_FILESYSTEM) == 28);
static_assert(util::to_underlying(Error::INVALID_PATTERN_VERSION) == 29);
static_assert(util::to_underlying(Error::OUT_OF_BUFFER_SPACE) == 30);
static_assert(util::to_underlying(Error::INVALID_SYSEX_FORMAT) == 31);
static_assert(util::to_underlying(Error::POS_PAST_STRING) == 32);
static_assert(util::to_underlying(Error::FILE_NOT_SAVED) == 33);
static_assert(util::to_underlying(Error::TOO_MANY_OPEN_STREAMS) == 34);
static_assert(util::to_underlying(Error::LOAD_IN_PROGRESS) == 35);

// DelugeAudioLoadResult as numbers. The Rust loader's `LoadResult` is checked against bindgen's
// binding in the BSP build, and also compiles in carriers with no bindgen output
// (crates/deluge_audio_loader_host), where it is pinned to these same literals -- so a reorder on
// either side fails to build rather than silently reinterpreting every field after it.
static_assert(sizeof(DelugeAudioLoadResult) == 312);
static_assert(alignof(DelugeAudioLoadResult) == 8);
static_assert(offsetof(DelugeAudioLoadResult, geometry) == 0);
static_assert(offsetof(DelugeAudioLoadResult, size_bytes) == 40);
static_assert(offsetof(DelugeAudioLoadResult, alternate_path) == 48);
static_assert(offsetof(DelugeAudioLoadResult, error) == 304);

// The remaining values the Rust side hand-duplicates from this header. DELUGE_MAX_PATH is also the
// stride of the candidate buffer and the width of DelugeAudioLoadResult::alternate_path, so a change
// here that is not mirrored would misread every candidate slot after the first.
static_assert(DELUGE_MAX_PATH == 256);
// The one whose drift silently reinstates a bug rather than causing a mismatch: if this stopped
// being the value the Rust side refuses, `deluge_audio_load_request` would clamp it to the candidate
// bound and read a buffer the header says must not be read.
static_assert(DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED == 0xFFFFFFFFu);
static_assert(DELUGE_AUDIO_LOAD_PENDING == 0u);
static_assert(DELUGE_AUDIO_LOAD_READY == 1u);
static_assert(DELUGE_AUDIO_LOAD_UNKNOWN == 2u);
// The Rust side bounds its candidate list by its own copy of this.
static_assert(AudioFileManager::kMaxLoadCandidates == 3);

} // namespace

/// Map a byte crossing the ABI to `Error` by number (see the static_asserts above). An out-of-range
/// byte degrades to `Error::UNSPECIFIED` rather than producing an invalid enum value via
/// `static_cast` -- the same fail-safe direction `raw_data_format_from_u8`
/// (crates/deluge_sample_fill/src/fill_logic.rs) takes for the analogous RawDataFormat byte below.
///
/// Declared in audio_load_abi.h rather than kept file-local: the C++ collector that consumes
/// `DelugeAudioLoadResult::error` needs exactly this mapping, and a second copy of it is a second
/// thing to keep in step with the asserts above.
Error deluge::storage::errorFromAbiByte(uint8_t v) noexcept {
	switch (v) {
	case 0:
		return Error::NONE;
	case 1:
		return Error::INSUFFICIENT_RAM;
	case 2:
		return Error::UNSPECIFIED;
	case 3:
		return Error::SD_CARD;
	case 4:
		return Error::NO_FURTHER_PRESETS;
	case 5:
		return Error::FILE_CORRUPTED;
	case 6:
		return Error::FILE_UNREADABLE;
	case 7:
		return Error::FILE_UNSUPPORTED;
	case 8:
		return Error::FILE_FIRMWARE_VERSION_TOO_NEW;
	case 9:
		return Error::RESULT_TAG_UNUSED;
	case 10:
		return Error::FOLDER_DOESNT_EXIST;
	case 11:
		return Error::WRITE_PROTECTED;
	case 12:
		return Error::BUG;
	case 13:
		return Error::WRITE_FAIL;
	case 14:
		return Error::FILE_TOO_BIG;
	case 15:
		return Error::PRESET_IN_USE;
	case 16:
		return Error::NO_FURTHER_FILES_THIS_DIRECTION;
	case 17:
		return Error::FILE_ALREADY_EXISTS;
	case 18:
		return Error::FILE_NOT_FOUND;
	case 19:
		return Error::ABORTED_BY_USER;
	case 20:
		return Error::MAX_FILE_SIZE_REACHED;
	case 21:
		return Error::SD_CARD_FULL;
	case 22:
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
	case 23:
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO;
	case 24:
		return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
	case 25:
		return Error::NO_ERROR_BUT_GET_OUT;
	case 26:
		return Error::INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE;
	case 27:
		return Error::SD_CARD_NOT_PRESENT;
	case 28:
		return Error::SD_CARD_NO_FILESYSTEM;
	case 29:
		return Error::INVALID_PATTERN_VERSION;
	case 30:
		return Error::OUT_OF_BUFFER_SPACE;
	case 31:
		return Error::INVALID_SYSEX_FORMAT;
	case 32:
		return Error::POS_PAST_STRING;
	case 33:
		return Error::FILE_NOT_SAVED;
	case 34:
		return Error::TOO_MANY_OPEN_STREAMS;
	case 35:
		return Error::LOAD_IN_PROGRESS;
	default:
		return Error::UNSPECIFIED; // out-of-range byte: degrade safely rather than UB-cast.
	}
}

namespace {

/// Never called; exists only to be COMPILED. An exhaustive switch over `Error` ITSELF (not the byte
/// crossing the boundary) with no `default` case: this repo builds with `-Werror=switch`
/// (CMakeLists.txt), so the compiler refuses to build the moment a new `Error` enumerator is added
/// upstream and not listed here. This is the count-completeness half the per-value static_asserts
/// above cannot provide alone -- they stay green if a value is simply APPENDED, since errorFromAbiByte's
/// `default` would just keep degrading the new byte to `Error::UNSPECIFIED` without anyone noticing.
[[maybe_unused]] constexpr void errorEnumerationIsComplete(Error e) noexcept {
	switch (e) {
	case Error::NONE:
	case Error::INSUFFICIENT_RAM:
	case Error::UNSPECIFIED:
	case Error::SD_CARD:
	case Error::NO_FURTHER_PRESETS:
	case Error::FILE_CORRUPTED:
	case Error::FILE_UNREADABLE:
	case Error::FILE_UNSUPPORTED:
	case Error::FILE_FIRMWARE_VERSION_TOO_NEW:
	case Error::RESULT_TAG_UNUSED:
	case Error::FOLDER_DOESNT_EXIST:
	case Error::WRITE_PROTECTED:
	case Error::BUG:
	case Error::WRITE_FAIL:
	case Error::FILE_TOO_BIG:
	case Error::PRESET_IN_USE:
	case Error::NO_FURTHER_FILES_THIS_DIRECTION:
	case Error::FILE_ALREADY_EXISTS:
	case Error::FILE_NOT_FOUND:
	case Error::ABORTED_BY_USER:
	case Error::MAX_FILE_SIZE_REACHED:
	case Error::SD_CARD_FULL:
	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE:
	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
	case Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP:
	case Error::NO_ERROR_BUT_GET_OUT:
	case Error::INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
	case Error::SD_CARD_NOT_PRESENT:
	case Error::SD_CARD_NO_FILESYSTEM:
	case Error::INVALID_PATTERN_VERSION:
	case Error::OUT_OF_BUFFER_SPACE:
	case Error::INVALID_SYSEX_FORMAT:
	case Error::POS_PAST_STRING:
	case Error::FILE_NOT_SAVED:
	case Error::TOO_MANY_OPEN_STREAMS:
	case Error::LOAD_IN_PROGRESS:
	// Listed for completeness only. This one never crosses the audio-load ABI: it is the BROWSER's
	// deferral (a folder listing waiting on the directory channel), and the loader has no
	// number for it. If one is ever needed, give it its own byte -- do not reuse 35, which is
	// LOAD_IN_PROGRESS and means something a caller acts on differently.
	case Error::LISTING_DEFERRED:
		break;
	}
}

static_assert(util::to_underlying(RawDataFormat::NATIVE) == 0);
static_assert(util::to_underlying(RawDataFormat::FLOAT) == 1);
static_assert(util::to_underlying(RawDataFormat::UNSIGNED_8) == 2);
static_assert(util::to_underlying(RawDataFormat::ENDIANNESS_WRONG_16) == 3);
static_assert(util::to_underlying(RawDataFormat::ENDIANNESS_WRONG_24) == 4);
static_assert(util::to_underlying(RawDataFormat::ENDIANNESS_WRONG_32) == 5);

/// Map a byte crossing the ABI to `RawDataFormat` by number -- see errorFromAbiByte's doc comment for
/// why never `static_cast`. Unlike errorFromAbiByte, an out-of-range byte here is REFUSED (empty
/// `optional`), not degraded to a default: this is one geometry field the synchronous path can never
/// receive out of range (`AudioFile::loadFile` assigns a `RawDataFormat` straight from the parser, so
/// an invalid value cannot arise there), and silently accepting a bad one would publish a sample that
/// renders wrong with no error -- exactly the failure shape this whole boundary exists to avoid.
std::optional<RawDataFormat> tryRawDataFormatFromByte(uint8_t v) noexcept {
	switch (v) {
	case 0:
		return RawDataFormat::NATIVE;
	case 1:
		return RawDataFormat::FLOAT;
	case 2:
		return RawDataFormat::UNSIGNED_8;
	case 3:
		return RawDataFormat::ENDIANNESS_WRONG_16;
	case 4:
		return RawDataFormat::ENDIANNESS_WRONG_24;
	case 5:
		return RawDataFormat::ENDIANNESS_WRONG_32;
	default:
		return std::nullopt;
	}
}

} // namespace

namespace deluge::storage::sample_build {

SampleBuild* begin(std::string_view filePath, std::string_view alternatePath, uint64_t sizeBytes,
                   Error* error) noexcept {
	Sample* sample = nullptr;
	try {
		Error beginError = Error::NONE;
		sample = audioFileManager.beginSampleFromCard(std::string{filePath}, std::string{alternatePath}, sizeBytes,
		                                              &beginError);
		if (sample == nullptr) {
			report(error, beginError);
			return nullptr;
		}
		auto* build = new SampleBuild{.sample = sample, .sizeBytes = sizeBytes};
		report(error, Error::NONE);
		return build;
	}
	// Catches EVERYTHING, not just deluge::exception: this repo's own allocators only ever throw
	// that, but a stray std::length_error out of std::string (or anything else) unwinding out of
	// this noexcept function is exactly the silent-terminate() outcome this file's hardening exists
	// to prevent -- a narrower catch would leave that door open.
	catch (...) {
		if (sample != nullptr) {
			// beginSampleFromCard succeeded -- adopting and reasoning `sample` -- before the
			// allocation that just threw (the SampleBuild itself). Destroy it the same way abandon()
			// would, so this doesn't orphan an adopted, reasoned, zero-geometry Sample with no build
			// left to close it. Nested try: if THIS throws too (e.g. another allocation failure while
			// destroying), there is nothing further to do safely, so swallow it rather than let a
			// second exception escape this noexcept function.
			try {
				Error destroyError = Error::INSUFFICIENT_RAM;
				(void)audioFileManager.finishSampleFromCard(sample, sizeBytes, &destroyError);
			} catch (...) {}
		}
		report(error, Error::INSUFFICIENT_RAM);
		return nullptr;
	}
}

bool resolveAlternateDir(AudioLoadContext* context, Error* error) noexcept {
	try {
		Error resolveError = Error::NONE;
		AudioLoadContext noLoad;
		const bool exists =
		    audioFileManager.resolveAlternateLoadDirStatus((context != nullptr) ? *context : noLoad, &resolveError);
		report(error, resolveError);
		return exists;
	} catch (...) {
		report(error, Error::INSUFFICIENT_RAM);
		return false;
	}
}

uint32_t candidates(const AudioLoadContext* context, std::string_view filePath, char* out,
                    uint32_t maxCandidates) noexcept {
	try {
		std::string proposed[AudioFileManager::kMaxLoadCandidates];
		const size_t capacity = std::min<size_t>(maxCandidates, AudioFileManager::kMaxLoadCandidates);
		const AudioLoadContext noLoad;
		const size_t count = audioFileManager.proposeLoadCandidates((context != nullptr) ? *context : noLoad,
		                                                            std::string{filePath}, proposed, capacity);
		const size_t written = std::min(count, capacity);
		for (size_t i = 0; i < written; i++) {
			if (proposed[i].size() + 1 > DELUGE_MAX_PATH) {
				// A candidate too long to fit is a caller-visible failure, not a silent snprintf
				// truncation the loader would go on to open -- and either miss, or worse, hit a
				// different real file sitting at the truncated path.
				return DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED;
			}
			std::snprintf(out + (i * DELUGE_MAX_PATH), DELUGE_MAX_PATH, "%s", proposed[i].c_str());
		}
		return static_cast<uint32_t>(count);
	} catch (...) {
		return DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED; // `out` is not trustworthy either way.
	}
}

Error bindReadStream(SampleBuild* build, std::string_view path) noexcept {
	SampleBuild* live = validateBuild(build);
	if (live == nullptr) {
		return Error::BUG; // invalid/consumed build: a caller bug, not a file problem
	}
	try {
		// SampleStream::open_read_stream owns the deluge_sample_stream registry handle (a DIFFERENT
		// handle space from whatever efatfs handle the loader used to read/parse the header) and its
		// own TOO_MANY_OPEN_STREAMS reclaim retry -- forward to it rather than reimplementing either.
		return live->sample->stream().open_read_stream(path);
	} catch (...) {
		return Error::INSUFFICIENT_RAM;
	}
}

bool applyGeometry(SampleBuild* build, const Geometry& geometry, Error* error) noexcept {
	SampleBuild* live = validateBuild(build);
	if (live == nullptr) {
		// A caller bug -- refuse rather than silently no-op, so the caller cannot go on to `finish` a
		// build nothing was ever written to.
		report(error, Error::BUG);
		return false;
	}
	const std::optional<RawDataFormat> rawDataFormat = tryRawDataFormatFromByte(geometry.raw_data_format);
	if (!rawDataFormat.has_value()) {
		// Refuse outright rather than degrade: unlike every other field here, the synchronous path
		// structurally cannot receive an invalid RawDataFormat (AudioFile::loadFile assigns it
		// straight from the parser), so accepting one would be the async path inventing a failure
		// mode -- silent wrong-geometry render -- that has no sync-path analogue at all.
		report(error, Error::BUG);
		return false;
	}
	Sample* s = live->sample;
	s->numChannels = geometry.num_channels;
	s->byteDepth = geometry.byte_depth;
	s->rawDataFormat = *rawDataFormat;
	s->sampleRate = geometry.sample_rate;
	s->audioDataStartPosBytes = geometry.audio_data_start_pos_bytes;
	s->audioDataLengthBytes = geometry.audio_data_length_bytes;
	s->fileLoopStartSamples = geometry.file_loop_start_samples;
	s->fileLoopEndSamples = geometry.file_loop_end_samples;
	// -1 == the file specified no root note (matches AudioFile::loadFile's header->midiNote.value_or(-1.0f)).
	s->midiNoteFromFile = (geometry.midi_note_present != 0) ? geometry.midi_note_value : -1.0f;
	s->waveTableCycleSize = geometry.wavetable_cycle_size;
	s->fileExplicitlySpecifiesSelfAsWaveTable = (geometry.wavetable_explicitly_specifies_self != 0);
	report(error, Error::NONE);
	return true;
}

AudioFile* finish(SampleBuild* build, Error errorIn, Error* error) noexcept {
	SampleBuild* raw = validateBuild(build);
	if (raw == nullptr) {
		report(error, Error::BUG);
		return nullptr;
	}
	raw->magic = SampleBuild::kConsumed;     // consumed now -- a later call through this pointer is a caller
	                                         // bug, not a live build (see validateBuild()).
	std::unique_ptr<SampleBuild> owned{raw}; // guarantees the build itself is freed on every path below,
	                                         // including the exception path.
	Error outcome = errorIn;
	AudioFile* published = nullptr;
	try {
		published = audioFileManager.finishSampleFromCard(owned->sample, owned->sizeBytes, &outcome);
	} catch (...) {
		outcome = Error::INSUFFICIENT_RAM;
		published = nullptr;
	}
	report(error, outcome);
	return published;
}

void abandon(SampleBuild* build) noexcept {
	SampleBuild* raw = validateBuild(build);
	if (raw == nullptr) {
		return;
	}
	raw->magic = SampleBuild::kConsumed;
	std::unique_ptr<SampleBuild> owned{raw};
	try {
		Error error = Error::FILE_UNREADABLE; // any non-NONE drives finishSampleFromCard's destroy path
		(void)audioFileManager.finishSampleFromCard(owned->sample, owned->sizeBytes, &error);
	} catch (...) {
		// Best-effort: if finishSampleFromCard threw mid-destroy, the Sample's exact state is no
		// longer knowable from here -- but the build itself is still freed either way (`owned`'s
		// destructor runs regardless of this catch).
	}
}

} // namespace deluge::storage::sample_build
