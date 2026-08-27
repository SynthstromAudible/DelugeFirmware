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
/// (proposeLoadCandidates), and the one I/O probe the async loader must sequence before it
/// (resolveAlternateLoadDirStatus). No new logic lives here -- see audio_load.h for the contract each
/// function keeps, in particular the build ownership rule.

#include "libdeluge/audio_load.h"

#include "model/sample/sample.h"
#include "storage/audio/audio_file_format.h" // RawDataFormat
#include "storage/audio/audio_file_manager.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
/// A build in flight: the Sample plus the sizeBytes finishSampleFromCard needs for finalizeAfterLoad
/// (buildAudioFileFromCard's caller resolves this once and threads it through both halves; a direct
/// caller across this boundary has to carry it the same way, since Sample itself has nowhere to park
/// it before finalizeAfterLoad runs).
struct BuildHandle {
	Sample* sample;
	uint64_t sizeBytes;
};
} // namespace

extern "C" DelugeSampleBuild* deluge_audio_load_begin(const char* file_path, const char* alternate_path,
                                                      uint64_t size_bytes, uint8_t* error_out) {
	Error error = Error::NONE;
	const std::string filePath{file_path};
	const std::string alternate{alternate_path};
	Sample* sample = audioFileManager.beginSampleFromCard(filePath, alternate, size_bytes, &error);
	*error_out = static_cast<uint8_t>(error);
	if (sample == nullptr) {
		return nullptr;
	}
	auto* handle = new BuildHandle{sample, size_bytes};
	return reinterpret_cast<DelugeSampleBuild*>(handle);
}

extern "C" bool deluge_audio_load_resolve_alternate_dir(uint8_t* error_out) {
	Error error = Error::NONE;
	const bool exists = audioFileManager.resolveAlternateLoadDirStatus(&error);
	*error_out = static_cast<uint8_t>(error);
	return exists;
}

extern "C" uint32_t deluge_audio_load_candidates(const char* file_path, char* out, uint32_t max_candidates) {
	std::string candidates[AudioFileManager::kMaxLoadCandidates];
	const size_t capacity = std::min<size_t>(max_candidates, AudioFileManager::kMaxLoadCandidates);
	const size_t count = audioFileManager.proposeLoadCandidates(std::string{file_path}, candidates, capacity);
	const size_t written = std::min(count, capacity);
	for (size_t i = 0; i < written; i++) {
		std::snprintf(out + (i * DELUGE_MAX_PATH), DELUGE_MAX_PATH, "%s", candidates[i].c_str());
	}
	return static_cast<uint32_t>(count);
}

extern "C" void deluge_audio_load_apply_geometry(DelugeSampleBuild* build, uint8_t num_channels, uint8_t byte_depth,
                                                 uint8_t raw_data_format, uint32_t sample_rate,
                                                 uint32_t audio_data_start_pos_bytes, uint32_t audio_data_length_bytes,
                                                 uint32_t file_loop_start_samples, uint32_t file_loop_end_samples,
                                                 uint8_t midi_note_present, float midi_note_value,
                                                 uint32_t wavetable_cycle_size,
                                                 uint8_t wavetable_explicitly_specifies_self) {
	Sample* s = reinterpret_cast<BuildHandle*>(build)->sample;
	s->numChannels = num_channels;
	s->byteDepth = byte_depth;
	s->rawDataFormat = static_cast<RawDataFormat>(raw_data_format);
	s->sampleRate = sample_rate;
	s->audioDataStartPosBytes = audio_data_start_pos_bytes;
	s->audioDataLengthBytes = audio_data_length_bytes;
	s->fileLoopStartSamples = file_loop_start_samples;
	s->fileLoopEndSamples = file_loop_end_samples;
	// -1 == the file specified no root note (matches AudioFile::loadFile's header->midiNote.value_or(-1.0f)).
	s->midiNoteFromFile = (midi_note_present != 0) ? midi_note_value : -1.0f;
	s->waveTableCycleSize = wavetable_cycle_size;
	s->fileExplicitlySpecifiesSelfAsWaveTable = (wavetable_explicitly_specifies_self != 0);
}

extern "C" bool deluge_audio_load_finish(DelugeSampleBuild* build, uint8_t error_in) {
	auto* handle = reinterpret_cast<BuildHandle*>(build);
	Error error = static_cast<Error>(error_in);
	AudioFile* published = audioFileManager.finishSampleFromCard(handle->sample, handle->sizeBytes, &error);
	delete handle;
	return published != nullptr;
}

extern "C" void deluge_audio_load_abandon(DelugeSampleBuild* build) {
	auto* handle = reinterpret_cast<BuildHandle*>(build);
	Error error = Error::FILE_UNREADABLE; // any non-NONE drives finishSampleFromCard's destroy path
	(void)audioFileManager.finishSampleFromCard(handle->sample, handle->sizeBytes, &error);
	delete handle;
}

// Weak fallbacks: BSPs without the Rust loader (legacy/host-cooperative sim, rza1) resolve these and
// always take the synchronous path. The Rust Embassy BSP overrides them in audio_loader.rs.
extern "C" __attribute__((weak)) bool deluge_audio_load_async_active() {
	return false;
}

extern "C" __attribute__((weak)) void deluge_audio_load_enqueue(const char* /*file_path*/) {
	// No async loader on this BSP/config; the caller never reaches here because
	// deluge_audio_load_async_active() is false.
}
