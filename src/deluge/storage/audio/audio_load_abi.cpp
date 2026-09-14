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

/// libdeluge/audio_load.h's implementation: thin adapters over AudioFileManager's no-I/O
/// prologue/epilogue (beginSampleFromCard/finishSampleFromCard), the I/O-free candidate policy
/// (proposeLoadCandidates), the one I/O probe the async loader must sequence before it
/// (resolveAlternateLoadDirStatus), and the streaming read-handle bind (SampleStream::open_read_stream)
/// a published build must have. See audio_load.h for the contract each function keeps, in particular
/// the build ownership rule and the call order.
///
/// Every function here is `noexcept`: the repo's allocators throw `deluge::exception::BAD_ALLOC`
/// (operators.cpp), and letting that (or anything else) unwind out of an `extern "C"` frame into the
/// Rust caller on the other side of this boundary is undefined behaviour -- on this target it
/// presents as `std::terminate()` aborting silently, i.e. an unexplained device death with no error
/// message. Each function catches with `catch (...)`, not just `deluge::exception` -- the repo's own
/// allocators only ever throw that, but anything else escaping would be the exact silent-terminate()
/// outcome this exists to prevent -- and degrades to a safe return instead. Where a catch handler
/// itself calls back into C++ (deluge_audio_load_begin's cleanup path), that call is wrapped in its
/// own nested try/catch so a second exception can't escape either.

#include "libdeluge/audio_load.h"

#include "definitions_cxx.hpp" // kFilenameBufferSize
#include "model/sample/sample.h"
#include "storage/audio/audio_file_format.h" // RawDataFormat
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/audio_load_abi.h"
#include "util/misc.h" // util::to_underlying

#include <algorithm>
#include <cstddef> // offsetof
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

static_assert(DELUGE_MAX_PATH == kFilenameBufferSize,
              "DELUGE_MAX_PATH documents itself as matching this codebase's existing whole-path ceiling");

namespace {

/// Marks a `BuildHandle` as live. Arbitrary but stable -- not a format tag, just a canary.
constexpr uint32_t kBuildMagic = 0x4C445342u; // "LDSB"
/// Marks a `BuildHandle` as consumed (finished or abandoned already), written just before it is
/// freed. See validateHandle() for what this can and cannot catch.
constexpr uint32_t kBuildMagicConsumed = 0u;

/// A build in flight: the Sample plus the sizeBytes finishSampleFromCard needs for finalizeAfterLoad
/// (buildAudioFileFromCard's caller resolves this once and threads it through both halves; a direct
/// caller across this boundary has to carry it the same way, since Sample itself has nowhere to park
/// it before finalizeAfterLoad runs). `magic` is consumed-tracking only -- see validateHandle().
struct BuildHandle {
	uint32_t magic = kBuildMagic;
	Sample* sample;
	uint64_t sizeBytes;
};

/// Validate an opaque `DelugeSampleBuild*` before touching it. NULL and an already-consumed handle
/// both come back as "not a live build" instead of being dereferenced further -- the header promises
/// `finish`/`abandon`/`apply_geometry` are safe against exactly these.
///
/// Reading `handle->magic` on a genuinely freed or foreign pointer is still, strictly, undefined
/// behaviour -- there is no way to validate an arbitrary opaque pointer without a registry this
/// boundary doesn't keep, and a sanitizer build may well report the read itself as a
/// heap-use-after-free (which is a perfectly good "fails loudly" outcome). What this DOES reliably
/// catch, on a build without a sanitizer attached, is the realistic caller bug -- reusing the same
/// handle across two finish/abandon calls -- turning it into a clean refusal instead of dispatching
/// straight through to a Sample that has already been destructed.
BuildHandle* validateHandle(DelugeSampleBuild* build) noexcept {
	auto* handle = reinterpret_cast<BuildHandle*>(build);
	if (handle == nullptr || handle->magic != kBuildMagic) {
		return nullptr;
	}
	return handle;
}

// Every value `error_in`/`error_out` can carry across this boundary is matched by NUMBER
// (errorFromAbiByte below), never `static_cast<Error>(byte)` -- `enum class Error` has no fixed
// underlying type, so its valid range is whatever `int` gives it and casting an out-of-range byte is
// undefined behaviour. These static_asserts pin the numeric identity errorFromAbiByte depends on:
// inserting a new Error enumerator upstream (definitions_cxx.hpp) shifts every later value, and
// without this, that shift would be silent -- every gate staying green while a caller across this
// boundary mismatches every code from the insertion point on.
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

// The request/collect boundary's two POD structs are hand-declared on BOTH sides: audio_load.h is
// not in the Rust BSP's wrapper.h, so bindgen does not generate them and nothing else cross-checks
// them. Pinning the size alone would not be enough -- swapping two same-width fields keeps every
// size identical and compiles clean on both sides, which is exactly the "loads without error,
// renders wrong" failure this file's own doc comment calls the worst available shape. These mirror
// the `const _` assertions in src/bsp/rust/src/audio_loader.rs, so a reorder on either side fails
// to build rather than silently reinterpreting every field after it.
static_assert(sizeof(DelugeAudioLoadGeometry) == 36);
static_assert(alignof(DelugeAudioLoadGeometry) == 4);
static_assert(offsetof(DelugeAudioLoadGeometry, sample_rate) == 0);
static_assert(offsetof(DelugeAudioLoadGeometry, audio_data_start_pos_bytes) == 4);
static_assert(offsetof(DelugeAudioLoadGeometry, audio_data_length_bytes) == 8);
static_assert(offsetof(DelugeAudioLoadGeometry, file_loop_start_samples) == 12);
static_assert(offsetof(DelugeAudioLoadGeometry, file_loop_end_samples) == 16);
static_assert(offsetof(DelugeAudioLoadGeometry, wavetable_cycle_size) == 20);
static_assert(offsetof(DelugeAudioLoadGeometry, midi_note_value) == 24);
static_assert(offsetof(DelugeAudioLoadGeometry, num_channels) == 28);
static_assert(offsetof(DelugeAudioLoadGeometry, byte_depth) == 29);
static_assert(offsetof(DelugeAudioLoadGeometry, raw_data_format) == 30);
static_assert(offsetof(DelugeAudioLoadGeometry, midi_note_present) == 31);
static_assert(offsetof(DelugeAudioLoadGeometry, wavetable_explicitly_specifies_self) == 32);

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
/// Declared in audio_load_abi.h (a C++-only header, not part of the ABI) rather than kept file-local:
/// the C++ collector that consumes `DelugeAudioLoadResult::error` needs exactly this mapping, and a
/// second copy of it is a second thing to keep in step with the asserts above.
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
	// deferral (a folder listing waiting on the directory channel), and `errorToAbiByte` has no
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

extern "C" DelugeSampleBuild* deluge_audio_load_begin(const char* file_path, const char* alternate_path,
                                                      uint64_t size_bytes, uint8_t* error_out) noexcept {
	Sample* sample = nullptr;
	try {
		Error error = Error::NONE;
		const std::string filePath{file_path};
		const std::string alternate{alternate_path};
		sample = audioFileManager.beginSampleFromCard(filePath, alternate, size_bytes, &error);
		if (sample == nullptr) {
			if (error_out != nullptr) {
				*error_out = static_cast<uint8_t>(error);
			}
			return nullptr;
		}
		auto* handle = new BuildHandle{.sample = sample, .sizeBytes = size_bytes};
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(Error::NONE);
		}
		return reinterpret_cast<DelugeSampleBuild*>(handle);
	}
	// Catches EVERYTHING, not just deluge::exception: this repo's own allocators only ever throw
	// that, but a stray std::length_error out of std::string (or anything else) unwinding out of
	// this noexcept, extern "C" function is exactly the silent-terminate() outcome this file's
	// hardening exists to prevent -- a narrower catch would leave that door open.
	catch (...) {
		if (sample != nullptr) {
			// beginSampleFromCard succeeded -- adopting and reasoning `sample` -- before the
			// allocation that just threw (the BuildHandle wrapper itself). Destroy it the same way
			// abandon() would, so this doesn't orphan an adopted, reasoned, zero-geometry Sample with
			// no handle left to close it. Nested try: if THIS throws too (e.g. another allocation
			// failure while destroying), there is nothing further to do safely, so swallow it rather
			// than let a second exception escape this noexcept function.
			try {
				Error destroyError = Error::INSUFFICIENT_RAM;
				(void)audioFileManager.finishSampleFromCard(sample, size_bytes, &destroyError);
			} catch (...) {}
		}
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(Error::INSUFFICIENT_RAM);
		}
		return nullptr;
	}
}

extern "C" bool deluge_audio_load_resolve_alternate_dir(DelugeAudioLoadContext* context, uint8_t* error_out) noexcept {
	try {
		Error error = Error::NONE;
		deluge::storage::AudioLoadContext noLoad;
		const bool exists = audioFileManager.resolveAlternateLoadDirStatus(
		    (context != nullptr) ? *deluge::storage::fromAbi(context) : noLoad, &error);
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(error);
		}
		return exists;
	} catch (...) {
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(Error::INSUFFICIENT_RAM);
		}
		return false;
	}
}

extern "C" uint32_t deluge_audio_load_candidates(const DelugeAudioLoadContext* context, const char* file_path,
                                                 char* out, uint32_t max_candidates) noexcept {
	try {
		std::string candidates[AudioFileManager::kMaxLoadCandidates];
		const size_t capacity = std::min<size_t>(max_candidates, AudioFileManager::kMaxLoadCandidates);
		const deluge::storage::AudioLoadContext noLoad;
		const size_t count =
		    audioFileManager.proposeLoadCandidates((context != nullptr) ? *deluge::storage::fromAbi(context) : noLoad,
		                                           std::string{file_path}, candidates, capacity);
		const size_t written = std::min(count, capacity);
		for (size_t i = 0; i < written; i++) {
			if (candidates[i].size() + 1 > DELUGE_MAX_PATH) {
				// A candidate too long to fit is a caller-visible failure, not a silent snprintf
				// truncation the loader would go on to open -- and either miss, or worse, hit a
				// different real file sitting at the truncated path.
				return DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED;
			}
			std::snprintf(out + (i * DELUGE_MAX_PATH), DELUGE_MAX_PATH, "%s", candidates[i].c_str());
		}
		return static_cast<uint32_t>(count);
	} catch (...) {
		return DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED; // `out` is not trustworthy either way.
	}
}

extern "C" uint8_t deluge_audio_load_bind_read_stream(DelugeSampleBuild* build, const char* path) noexcept {
	BuildHandle* handle = validateHandle(build);
	if (handle == nullptr) {
		return static_cast<uint8_t>(Error::BUG); // invalid/consumed build: a caller bug, not a file problem
	}
	try {
		// SampleStream::open_read_stream owns the deluge_sample_stream registry handle (a DIFFERENT
		// handle space from whatever efatfs handle the caller used to read/parse the header) and its
		// own TOO_MANY_OPEN_STREAMS reclaim retry -- forward to it rather than reimplementing either.
		return static_cast<uint8_t>(handle->sample->stream().open_read_stream(path));
	} catch (...) {
		return static_cast<uint8_t>(Error::INSUFFICIENT_RAM);
	}
}

extern "C" bool deluge_audio_load_apply_geometry(DelugeSampleBuild* build, const DelugeAudioLoadGeometry* geometry,
                                                 uint8_t* error_out) noexcept {
	BuildHandle* handle = validateHandle(build);
	if (handle == nullptr || geometry == nullptr) {
		// A caller bug (invalid/consumed build, or no geometry at all) -- refuse rather than
		// silently no-op, so the caller cannot go on to `finish` a build nothing was ever written to.
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(Error::BUG);
		}
		return false;
	}
	const std::optional<RawDataFormat> rawDataFormat = tryRawDataFormatFromByte(geometry->raw_data_format);
	if (!rawDataFormat.has_value()) {
		// Refuse outright rather than degrade: unlike every other field here, the synchronous path
		// structurally cannot receive an invalid RawDataFormat (AudioFile::loadFile assigns it
		// straight from the parser), so accepting one would be the async path inventing a failure
		// mode -- silent wrong-geometry render -- that has no sync-path analogue at all.
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(Error::BUG);
		}
		return false;
	}
	Sample* s = handle->sample;
	s->numChannels = geometry->num_channels;
	s->byteDepth = geometry->byte_depth;
	s->rawDataFormat = *rawDataFormat;
	s->sampleRate = geometry->sample_rate;
	s->audioDataStartPosBytes = geometry->audio_data_start_pos_bytes;
	s->audioDataLengthBytes = geometry->audio_data_length_bytes;
	s->fileLoopStartSamples = geometry->file_loop_start_samples;
	s->fileLoopEndSamples = geometry->file_loop_end_samples;
	// -1 == the file specified no root note (matches AudioFile::loadFile's header->midiNote.value_or(-1.0f)).
	s->midiNoteFromFile = (geometry->midi_note_present != 0) ? geometry->midi_note_value : -1.0f;
	s->waveTableCycleSize = geometry->wavetable_cycle_size;
	s->fileExplicitlySpecifiesSelfAsWaveTable = (geometry->wavetable_explicitly_specifies_self != 0);
	if (error_out != nullptr) {
		*error_out = static_cast<uint8_t>(Error::NONE);
	}
	return true;
}

extern "C" bool deluge_audio_load_finish(DelugeSampleBuild* build, uint8_t error_in, uint8_t* error_out,
                                         DelugeAudioFile** published_out) noexcept {
	if (published_out != nullptr) {
		// Cleared up front, so every failure exit below (including the invalid-handle one, which
		// returns before anything else runs) leaves it NULL rather than whatever the caller had there.
		*published_out = nullptr;
	}
	BuildHandle* raw = validateHandle(build);
	if (raw == nullptr) {
		if (error_out != nullptr) {
			*error_out = static_cast<uint8_t>(Error::BUG);
		}
		return false;
	}
	raw->magic = kBuildMagicConsumed;         // consumed now -- a later call through this pointer is a caller
	                                          // bug, not a live build (see validateHandle()).
	std::unique_ptr<BuildHandle> handle{raw}; // guarantees the wrapper itself is freed on every path
	                                          // below, including the exception path.
	Error error = deluge::storage::errorFromAbiByte(error_in);
	AudioFile* published = nullptr;
	try {
		published = audioFileManager.finishSampleFromCard(handle->sample, handle->sizeBytes, &error);
	} catch (...) {
		error = Error::INSUFFICIENT_RAM;
		published = nullptr;
	}
	if (error_out != nullptr) {
		*error_out = static_cast<uint8_t>(error);
	}
	if (published != nullptr && published_out != nullptr) {
		*published_out = reinterpret_cast<DelugeAudioFile*>(published);
	}
	return published != nullptr;
}

extern "C" void deluge_audio_load_abandon(DelugeSampleBuild* build) noexcept {
	BuildHandle* raw = validateHandle(build);
	if (raw == nullptr) {
		return;
	}
	raw->magic = kBuildMagicConsumed;
	std::unique_ptr<BuildHandle> handle{raw};
	try {
		Error error = Error::FILE_UNREADABLE; // any non-NONE drives finishSampleFromCard's destroy path
		(void)audioFileManager.finishSampleFromCard(handle->sample, handle->sizeBytes, &error);
	} catch (...) {
		// Best-effort: if finishSampleFromCard threw mid-destroy, the Sample's exact state is no
		// longer knowable from here -- but the BuildHandle wrapper itself is still freed either way
		// (`handle`'s destructor runs regardless of this catch).
	}
}

// Weak fallbacks: BSPs without the Rust loader (legacy/host-cooperative sim, rza1) resolve these and
// always take the synchronous path. The Rust Embassy BSP overrides them in audio_loader.rs.
extern "C" __attribute__((weak)) bool deluge_audio_load_async_active() noexcept {
	return false;
}

extern "C" __attribute__((weak)) bool deluge_audio_load_request(const char* /*file_path*/, const char* /*candidates*/,
                                                                uint32_t /*num_candidates*/) noexcept {
	// No async loader on this BSP/config; the caller never reaches here because
	// deluge_audio_load_async_active() is false. Refusing (rather than pretending to accept) keeps
	// a caller that ignores async_active() from waiting for a result nothing will ever produce.
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_audio_load_collect(const char* /*file_path*/,
                                                                   DelugeAudioLoadResult* /*out*/) noexcept {
	// Same as above. UNKNOWN, not PENDING: PENDING would tell a caller to keep asking forever.
	return DELUGE_AUDIO_LOAD_UNKNOWN;
}

#ifdef DELUGE_HOST
extern "C" __attribute__((weak)) void deluge_audio_load_set_latency_ms(uint32_t /*ms*/) noexcept {
	// A host build without the Rust loader has no loads to delay.
}
#endif
