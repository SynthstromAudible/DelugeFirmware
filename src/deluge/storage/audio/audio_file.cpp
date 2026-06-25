/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "storage/audio/audio_file.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_byte_source.h"
#include "storage/audio/audio_file_format.h"
#include "storage/wave_table/wave_table.h"

#include "deluge_resource.h" // resource manager: adopted AudioFile objects route reasons to leases

// Decode the file header (storage/audio/audio_file_format) then apply the result to this object: a Sample
// takes the parsed fields wholesale; a WaveTable hands them to WaveTable::setup. The chunk-by-chunk decode
// (and RIFF/AIFF container detection) lives in parseSampleHeader / parseWaveTableHeader — this method is just
// the type-dispatch the old fake-polymorphic casts hid, plus lowering the parser's optionals to Sample's
// legacy defaults.
Error AudioFile::loadFile(AudioByteSource& source, bool makeWaveTableWorkAtAllCosts, DeserializerByteSource* wtSource) {
	if (type == AudioFileType::WAVETABLE) {
		const std::expected<WaveTableHeader, Error> header = parseWaveTableHeader(source, makeWaveTableWorkAtAllCosts);
		if (!header) {
			return header.error();
		}
		numChannels = header->numChannels;
		return static_cast<WaveTable*>(this)->setupFromFile(
		    *wtSource, header->cycleSize, header->audioDataStartPosBytes, header->audioDataLengthBytes,
		    header->byteDepth, header->rawDataFormat);
	}

	const std::expected<SampleHeader, Error> header = parseSampleHeader(source);
	if (!header) {
		return header.error();
	}

	numChannels = header->numChannels;
	auto& sample = static_cast<Sample&>(*this);
	sample.byteDepth = header->byteDepth;
	sample.rawDataFormat = header->rawDataFormat;
	sample.sampleRate = header->sampleRate;
	sample.audioDataStartPosBytes = header->audioDataStartPosBytes;
	sample.audioDataLengthBytes = header->audioDataLengthBytes;
	sample.fileLoopStartSamples = header->fileLoopStartSamples;
	sample.fileLoopEndSamples = header->fileLoopEndSamples;
	sample.midiNoteFromFile = header->midiNote.value_or(-1.0f); // -1 == the file specified no root note.
	sample.waveTableCycleSize = header->waveTableCycleSize;
	sample.fileExplicitlySpecifiesSelfAsWaveTable = header->fileExplicitlySpecifiesSelfAsWaveTable;
	return Error::NONE;
}

void AudioFile::addReason() {
	// Adopted object: each reason is a hard lease (reason 0 ⇒ unleased ⇒ evictable). The lease count
	// lives in the manager's chunk slot (read via leaseCount()). The 0→1 transition makes the object
	// project-relevant (numReasonsIncreasedFromZero → soft-reference its assets, for a Sample).
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_add_lease(mgr, this);
	}
	if (leaseCount() == 1) {
		numReasonsIncreasedFromZero();
	}
}

void AudioFile::removeReason(char const* errorCode) {
	// Adopted object: drop the lease. At reason 0 it's unleased ⇒ evictable under pressure (the
	// manager value-scores it). The 1→0 transition drops project-relevance. release saturates at 0,
	// so there is no underflow to guard.
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_release(mgr, this);
	}
	if (leaseCount() == 0) {
		numReasonsDecreasedToZero(errorCode);
	}
}

uint32_t AudioFile::leaseCount() const {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	return (mgr != nullptr) ? deluge_resource_lease_count_by_slot(mgr, resourceSlot) : 0;
}

bool AudioFile::isProjectReferenced() const {
	return leaseCount() > 0;
}
