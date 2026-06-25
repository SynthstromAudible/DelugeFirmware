/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>
#include <expected>
#include <optional>

class AudioByteSource;

/// On-disk encoding of an audio file's raw sample words — how the bytes read off the card must be
/// reinterpreted to reach native q31 (see Sample::convertToNative). A facet of the file format, so it
/// lives here rather than on Sample.
enum class RawDataFormat : uint8_t {
	NATIVE = 0,
	FLOAT = 1,
	UNSIGNED_8 = 2,
	ENDIANNESS_WRONG_16 = 3,
	ENDIANNESS_WRONG_24 = 4,
	ENDIANNESS_WRONG_32 = 5,
};

/// The header facts a WAV/AIFF parse yields when the file is loaded as a Sample. A plain value the caller
/// applies to a Sample — no behaviour, no ownership: this is the format-decode boundary, the unit slated to
/// cross to Rust. "Not specified by the file" is modelled with `std::optional` rather than a sentinel.
struct SampleHeader {
	uint8_t numChannels;
	uint8_t byteDepth;
	RawDataFormat rawDataFormat;
	uint32_t sampleRate;
	uint32_t audioDataStartPosBytes; // Offset from the start of the file to the first audio byte.
	uint32_t audioDataLengthBytes;
	// 0 == no loop. This is the WAV/AIFF format's own convention (a degenerate 0,0 range), not a parser
	// sentinel, so it stays a plain value — and the AIFF marker resolution can set the two independently.
	uint32_t fileLoopStartSamples;
	uint32_t fileLoopEndSamples;
	std::optional<float> midiNote; // empty == the file specified no root note (was the -1 sentinel).
	// Carried so a later Sample→WaveTable conversion (makeWaveTableWorkAtAllCosts) has the file's hints.
	uint32_t waveTableCycleSize;
	bool fileExplicitlySpecifiesSelfAsWaveTable;
};

/// The header facts needed to load a file as a WaveTable — a tight subset of SampleHeader (no sample rate,
/// loops or root note). The parser has already validated mono + a known bit depth.
struct WaveTableHeader {
	uint8_t numChannels; // Validated == 1 (stereo can never be a wavetable).
	uint8_t byteDepth;
	RawDataFormat rawDataFormat;
	uint32_t audioDataStartPosBytes;
	uint32_t audioDataLengthBytes;
	uint32_t cycleSize; // Samples per cycle.
};

/// Parse a WAV/AIFF header off `src` as a Sample. Reads the 12-byte RIFF/FORM container header itself, then
/// scans every metadata chunk and resolves AIFF loop markers. Pure decode: no AudioFile is mutated and no DSP
/// runs. Returns the decoded header, or the first decode error (corrupt/unsupported).
std::expected<SampleHeader, Error> parseSampleHeader(AudioByteSource& src);

/// Parse a WAV/AIFF header off `src` as a WaveTable. Reads the container header, then scans until the audio
/// data chunk is located and validates it as a wavetable (mono, known bit depth, and — unless
/// `makeWaveTableWorkAtAllCosts` — a wavetable-specifying `clm ` tag or a wavetable-looking length; AIFF is
/// rejected outright unless insisted on). The caller then runs WaveTable::setup. Pure decode.
std::expected<WaveTableHeader, Error> parseWaveTableHeader(AudioByteSource& src, bool makeWaveTableWorkAtAllCosts);
