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

#include "storage/audio/audio_file_format.h"
#include "io/debug/log.h"
#include "storage/audio/audio_file_reader.h"
#include "util/functions.h"
#include "util/misc.h"
#include <array>
#include <cstdint>

namespace {
constexpr int32_t kMaxNumMarkers = 8;

// http://muratnkonar.com/aiff/
// https://sites.google.com/site/musicgapi/technical-documents/wav-file-format#wavefilechunks
} // namespace

Error parseAudioFileHeader(AudioFileReader& reader, AudioFileType type, bool makeWaveTableWorkAtAllCosts, bool isAiff,
                           AudioFileFormat& out) {
	const bool isSample = (type == AudioFileType::SAMPLE);
	const bool isWaveTable = (type == AudioFileType::WAVETABLE);

	// AIFF files will only be used for WaveTables if the user insists.
	if (isWaveTable && !makeWaveTableWorkAtAllCosts && isAiff) {
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
	}

	uint32_t bytePos = reader.getBytePos();

	Error error = Error::NONE;
	bool foundDataChunk = false; // Also applies to an AIFF file's SSND chunk.
	bool foundFmtChunk = false;  // Also applies to an AIFF file's COMM chunk.

	// AIFF marker bookkeeping — resolved into loop points once the whole header is scanned.
	int16_t sustainLoopBeginMarkerId = -1;
	int16_t sustainLoopEndMarkerId = -1;
	uint16_t numMarkers = 0;
	std::array<int16_t, kMaxNumMarkers> markerIDs{};
	std::array<uint32_t, kMaxNumMarkers> markerPositions{};

	// Validate the located audio data as a wavetable and report whether the caller should proceed to
	// WaveTable::setup. Shared by the WAV "data" and AIFF "SSND" chunks (neither can fall through to the
	// other, hence a callable rather than the legacy cross-switch goto).
	const auto finishWaveTable = [&]() -> Error {
		if (out.byteDepth == AudioFileFormat::kByteDepthUnset) {
			// Haven't found the "fmt " tag yet, so we don't know the bit depth. Shouldn't happen.
			return Error::FILE_UNSUPPORTED;
		}
		if (out.numChannels != 1) {
			return Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO; // Stereo files are never usable.
		}
		// If this isn't actually a wavetable-specifying file (nor a wavetable-looking length), and the
		// user isn't insisting, opt out.
		if (!out.fileExplicitlySpecifiesSelfAsWaveTable && !makeWaveTableWorkAtAllCosts) {
			const int32_t audioDataLengthSamples = out.audioDataLengthBytes / out.byteDepth;
			if ((audioDataLengthSamples & 2047) != 0) {
				return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
			}
		}
		return Error::NONE;
	};

	bool stop = false; // A mid-chunk read error abandons the scan and falls to the found-chunk check.

	while (bytePos < reader.fileSize && !stop) {

		struct {
			uint32_t name;
			uint32_t length;
		} thisChunk{};

		error = reader.readBytes(reinterpret_cast<char*>(&thisChunk), 4 * 2);
		if (error != Error::NONE) {
			break;
		}

		if (isAiff) {
			thisChunk.length = swapEndianness32(thisChunk.length);
		}

		const uint32_t bytesCurrentChunkNotRoundedUp = thisChunk.length;
		// If the chunk size is odd, skip the trailing padding byte too (a RIFF requirement).
		thisChunk.length = (thisChunk.length + 1) & ~static_cast<uint32_t>(1);

		const uint32_t bytePosOfThisChunkData = reader.getBytePos();
		bytePos = bytePosOfThisChunkData + thisChunk.length; // Where the next RIFF chunk begins.

		// ------ WAV ------
		if (!isAiff) {
			switch (thisChunk.name) {

			// Data chunk - "data"
			case charsToIntegerConstant('d', 'a', 't', 'a'): {
				foundDataChunk = true;
				out.audioDataStartPosBytes = bytePosOfThisChunkData;
				out.audioDataLengthBytes = bytesCurrentChunkNotRoundedUp;
				if (isWaveTable) {
					return finishWaveTable(); // Header done; the caller runs WaveTable::setup.
				}
				break;
			}

			// Format chunk - "fmt "
			case charsToIntegerConstant('f', 'm', 't', ' '): {
				foundFmtChunk = true;

				std::array<uint32_t, 4> header{};
				error = reader.readBytes(reinterpret_cast<char*>(header.data()), 4 * 4);
				if (error != Error::NONE) {
					return error;
				}

				// Bit depth.
				const uint16_t bits = header[3] >> 16;
				switch (bits) {
				case 8:
					out.rawDataFormat = RawDataFormat::UNSIGNED_8;
					[[fallthrough]];
				case 16:
				case 24:
				case 32:
					out.byteDepth = bits >> 3;
					break;
				case 256 ... 65535:
					__builtin_unreachable();
				default:
					return Error::FILE_UNSUPPORTED;
				}

				// Format.
				const uint16_t format = header[0];
				if (format == WAV_FORMAT_PCM) {}
				else if (format == WAV_FORMAT_FLOAT && out.byteDepth == 4) {
					out.rawDataFormat = RawDataFormat::FLOAT;
				}
				else {
					return Error::FILE_UNSUPPORTED;
				}

				// Num channels.
				out.numChannels = header[0] >> 16;
				if (out.numChannels != 1 && out.numChannels != 2) {
					return Error::FILE_UNSUPPORTED;
				}

				if (isSample) {
					out.sampleRate = header[1];
					if (out.sampleRate < 5000 || out.sampleRate > 96000) {
						return Error::FILE_UNSUPPORTED;
					}
				}
				break;
			}

			// Sample chunk - "smpl"
			case charsToIntegerConstant('s', 'm', 'p', 'l'): {
				if (isSample) {
					std::array<uint32_t, 9> data{};
					error = reader.readBytes(reinterpret_cast<char*>(data.data()), 4 * 9);
					if (error != Error::NONE) {
						break;
					}

					const uint32_t midiNote = data[3];
					const uint32_t midiPitchFraction = data[4];
					const uint32_t numLoops = data[7];

					if ((midiNote || midiPitchFraction) && midiNote < 128) {
						out.midiNoteFromFile =
						    static_cast<float>(midiPitchFraction) / (static_cast<uint64_t>(1) << 32) + midiNote;
					}

					if (numLoops == 1) {
						for (uint32_t l = 0; l < numLoops; l++) {
							D_PRINTLN("loop  %d", l);

							std::array<uint32_t, 6> loopData{};
							error = reader.readBytes(reinterpret_cast<char*>(loopData.data()), 4 * 6);
							if (error != Error::NONE) {
								stop = true;
								break;
							}

							out.fileLoopStartSamples = loopData[2];
							out.fileLoopEndSamples = loopData[3];
						}
					}
				}
				break;
			}

			// Instrument chunk - "inst"
			case charsToIntegerConstant('i', 'n', 's', 't'): {
				if (isSample) {
					std::array<uint8_t, 7> data{};
					error = reader.readBytes(reinterpret_cast<char*>(data.data()), 7);
					if (error != Error::NONE) {
						break;
					}

					const uint8_t midiNote = data[0];
					const int8_t fineTune = data[1];
					if (midiNote < 128) {
						out.midiNoteFromFile = static_cast<float>(midiNote) - static_cast<float>(fineTune) * 0.01;
					}
				}
				break;
			}

			// Serum wavetable chunk - "clm "
			case charsToIntegerConstant('c', 'l', 'm', ' '): {
				std::array<char, 7> data{};
				error = reader.readBytes(data.data(), 7);
				if (error != Error::NONE) {
					break;
				}

				if ((*reinterpret_cast<uint32_t*>(data.data()) & 0x00FFFFFF)
				    == charsToIntegerConstant('<', '!', '>', 0)) {
					out.fileExplicitlySpecifiesSelfAsWaveTable = true;
					const int32_t number = memToUIntOrError(&data[3], &data[7]);
					if (number >= 1) {
						out.waveTableCycleSize = number;
						D_PRINTLN("clm tag num samples per cycle:  %d", out.waveTableCycleSize);
					}
				}
				break;
			}
			}
		}

		// ------ AIFF ------
		else {
			switch (thisChunk.name) {

			// SSND
			case charsToIntegerConstant('S', 'S', 'N', 'D'): {
				foundDataChunk = true;

				uint32_t offset = 0;
				error = reader.readBytes(reinterpret_cast<char*>(&offset), 4);
				if (error != Error::NONE) {
					return error;
				}
				offset = swapEndianness32(offset);
				out.audioDataLengthBytes = bytesCurrentChunkNotRoundedUp - offset - 8;
				out.audioDataStartPosBytes = reader.getBytePos() + 4 + offset;

				if (isWaveTable) {
					return finishWaveTable();
				}
				break;
			}

			// COMM
			case charsToIntegerConstant('C', 'O', 'M', 'M'): {
				foundFmtChunk = true;

				if (thisChunk.length != 18) {
					return Error::FILE_UNSUPPORTED;
				}

				std::array<uint16_t, 9> header{};
				error = reader.readBytes(reinterpret_cast<char*>(header.data()), 18);
				if (error != Error::NONE) {
					return error;
				}

				// Num channels.
				out.numChannels = swapEndianness2x16(header[0]);
				if (out.numChannels != 1 && out.numChannels != 2) {
					return Error::FILE_UNSUPPORTED;
				}

				// Bit depth.
				const uint16_t bits = swapEndianness2x16(header[3]);
				if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
					return Error::FILE_UNSUPPORTED;
				}
				out.byteDepth = bits >> 3;

				if (out.byteDepth > 1) {
					out.rawDataFormat = static_cast<RawDataFormat>(
					    util::to_underlying(RawDataFormat::ENDIANNESS_WRONG_16) + out.byteDepth - 2);
				}

				if (isSample) {
					const uint32_t sampleRate = ConvertFromIeeeExtended(reinterpret_cast<unsigned char*>(&header[4]));
					if (sampleRate < 5000 || sampleRate > 96000) {
						return Error::FILE_UNSUPPORTED;
					}
					out.sampleRate = sampleRate;
				}
				break;
			}

			// MARK
			case charsToIntegerConstant('M', 'A', 'R', 'K'): {
				error = reader.readBytes(reinterpret_cast<char*>(&numMarkers), 2);
				if (error != Error::NONE) {
					break;
				}
				numMarkers = swapEndianness2x16(numMarkers);
				D_PRINTLN("numMarkers:  %d", numMarkers);

				if (numMarkers > kMaxNumMarkers) {
					numMarkers = kMaxNumMarkers;
				}

				for (int32_t m = 0; m < numMarkers && !stop; m++) {
					uint16_t markerId = 0;
					error = reader.readBytes(reinterpret_cast<char*>(&markerId), 2);
					if (error != Error::NONE) {
						stop = true;
						break;
					}
					markerIDs[m] = swapEndianness2x16(markerId);

					uint32_t markerPos = 0;
					error = reader.readBytes(reinterpret_cast<char*>(&markerPos), 4);
					if (error != Error::NONE) {
						stop = true;
						break;
					}
					markerPositions[m] = swapEndianness32(markerPos);

					uint8_t stringLength = 0;
					error = reader.readBytes(reinterpret_cast<char*>(&stringLength), 1);
					if (error != Error::NONE) {
						stop = true;
						break;
					}

					const uint32_t stringLengthRoundedUpToBeEven =
					    (static_cast<uint32_t>(stringLength) + 1) & ~static_cast<uint32_t>(1);
					// Cluster boundaries will be checked at the next read.
					reader.byteIndexWithinCluster += stringLengthRoundedUpToBeEven;
				}
				break;
			}

			// INST
			case charsToIntegerConstant('I', 'N', 'S', 'T'): {
				if (isSample) {
					std::array<uint8_t, 8> data{};
					error = reader.readBytes(reinterpret_cast<char*>(data.data()), 8);
					if (error != Error::NONE) {
						break;
					}

					const uint8_t midiNote = data[0];
					const int8_t fineTune = data[1];
					if ((midiNote || fineTune) && midiNote < 128) {
						out.midiNoteFromFile = static_cast<float>(midiNote) - static_cast<float>(fineTune) * 0.01;
						D_PRINTLN("unshifted note:  %s", out.midiNoteFromFile);
					}

					// Just read the sustain loop, which comes first.
					std::array<uint16_t, 3> loopData{};
					error = reader.readBytes(reinterpret_cast<char*>(loopData.data()), 3 * 2);
					if (error != Error::NONE) {
						break;
					}

					sustainLoopBeginMarkerId = swapEndianness2x16(loopData[1]);
					sustainLoopEndMarkerId = swapEndianness2x16(loopData[2]);
				}
				break;
			}
			}
		}

		if (!stop) {
			reader.jumpForwardToBytePos(bytePos);
		}
	}

	if (!foundDataChunk || !foundFmtChunk) {
		return Error::FILE_CORRUPTED;
	}

	// Sample-only finalisation (a wavetable returned from the data/SSND chunk above).
	if (isSample && isAiff && sustainLoopEndMarkerId != -1) {
		for (int32_t m = 0; m < numMarkers; m++) {
			if (markerIDs[m] == sustainLoopBeginMarkerId) {
				out.fileLoopStartSamples = markerPositions[m];
			}
			if (markerIDs[m] == sustainLoopEndMarkerId) {
				out.fileLoopEndSamples = markerPositions[m];
			}
		}
	}

	return Error::NONE;
}
