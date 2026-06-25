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

/// The header facts a WAV/AIFF parse yields — a plain value the parser fills and the caller applies to a
/// Sample (or feeds to WaveTable::setup). No behaviour, no ownership: this is the format-decode boundary,
/// the unit slated to cross to Rust. Defaults mirror Sample's freshly-constructed state so a field the
/// file never specifies leaves the Sample at its default when the descriptor is applied wholesale.
struct AudioFileFormat {
	/// `byteDepth == kByteDepthUnset` means no "fmt "/"COMM" chunk has been seen yet — doubles as the
	/// "format known?" gate while parsing (matches the legacy `255` sentinel).
	static constexpr uint8_t kByteDepthUnset = 255;

	uint8_t numChannels = 0;
	uint8_t byteDepth = kByteDepthUnset;
	RawDataFormat rawDataFormat = RawDataFormat::NATIVE;
	uint32_t sampleRate = 44100;
	uint32_t audioDataStartPosBytes = 0; // Offset from the start of the file to the first audio byte.
	uint32_t audioDataLengthBytes = 0;
	uint32_t fileLoopStartSamples = 0;
	uint32_t fileLoopEndSamples = 0;
	float midiNoteFromFile = -1;        // -1 means the file specified none.
	uint32_t waveTableCycleSize = 2048; // Samples per cycle if interpreted as a wavetable.
	bool fileExplicitlySpecifiesSelfAsWaveTable = false;
};

/// Parse a WAV (`isAiff == false`) or AIFF (`isAiff == true`) header off `src`, filling `out`. Reads only
/// the header/metadata chunks — for a WAVETABLE it stops once the data chunk is located (the caller then
/// runs WaveTable::setup); for a SAMPLE it scans every chunk and resolves AIFF loop markers. Pure decode: no
/// AudioFile is mutated and no DSP runs. Returns Error::NONE on success, or the first decode error
/// (corrupt/unsupported/not-loadable-as-wavetable).
Error parseAudioFileHeader(AudioByteSource& src, AudioFileType type, bool makeWaveTableWorkAtAllCosts, bool isAiff,
                           AudioFileFormat& out);
