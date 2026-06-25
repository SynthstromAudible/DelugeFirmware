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
#include "storage/audio/audio_byte_source.h"
#include "util/audio_format_helpers.h" // charsToIntegerConstant, swapEndianness*, memToUIntOrError, ConvertFromIeeeExtended
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <cstdint>

// http://muratnkonar.com/aiff/
// https://sites.google.com/site/musicgapi/technical-documents/wav-file-format#wavefilechunks

namespace {
constexpr int32_t kMaxNumMarkers = 8;

// The two RIFF/IFF containers we read.
enum class Container { Wav, Aiff };

// Whether we're decoding the file as a Sample or a WaveTable — selects which chunks matter and when the scan
// stops. Internal to the parser; the two public entry points keep it off the API.
enum class ParseMode { Sample, WaveTable };

// Mutable accumulator the chunk walk fills. "Field not yet seen" is an empty optional, not a sentinel; the
// public entry points lower it into the typed result.
struct HeaderFields {
	std::optional<uint8_t> numChannels;
	std::optional<uint8_t> byteDepth; // empty until a "fmt "/"COMM" chunk is seen (was the 255 sentinel).
	RawDataFormat rawDataFormat = RawDataFormat::NATIVE;
	uint32_t sampleRate = 44100;
	uint32_t audioDataStartPosBytes = 0; // Offset from the start of the file to the first audio byte.
	uint32_t audioDataLengthBytes = 0;
	uint32_t fileLoopStartSamples = 0;
	uint32_t fileLoopEndSamples = 0;
	std::optional<float> midiNote;
	uint32_t waveTableCycleSize = 2048;
	bool fileExplicitlySpecifiesSelfAsWaveTable = false;
	bool foundDataChunk = false; // Also applies to an AIFF file's SSND chunk.
	bool foundFmtChunk = false;  // Also applies to an AIFF file's COMM chunk.
};

// Read the 12-byte RIFF/WAVE or FORM/AIFF top header and classify the container.
std::expected<Container, Error> detectContainer(AudioByteSource& src) {
	std::array<uint32_t, 3> topHeader{};
	if (const Error error = src.read({reinterpret_cast<std::byte*>(topHeader.data()), sizeof(topHeader)});
	    error != Error::NONE) {
		return std::unexpected(error);
	}
	if (topHeader[0] == 0x46464952 && topHeader[2] == 0x45564157) { // "RIFF" / "WAVE"
		return Container::Wav;
	}
	if (topHeader[0] == 0x4D524F46 && topHeader[2] == 0x46464941) { // "FORM" / "AIFF"
		return Container::Aiff;
	}
	return std::unexpected(Error::FILE_UNSUPPORTED);
}

// Walk every header/metadata chunk after the container header, filling `f`. For a WaveTable it stops once the
// audio data chunk is located and validated; for a Sample it scans the whole header and resolves AIFF loop
// markers. Pure decode — no AudioFile is mutated. Returns the first decode error, or NONE.
Error walkChunks(AudioByteSource& src, Container container, ParseMode mode, bool makeWaveTableWorkAtAllCosts,
                 HeaderFields& f) {
	const bool isAiff = (container == Container::Aiff);
	const bool isSample = (mode == ParseMode::Sample);
	const bool isWaveTable = (mode == ParseMode::WaveTable);

	// Read `num` bytes into `dest` through the byte source (mirrors the old reader.readBytes shape).
	const auto readBytes = [&](void* dest, int32_t num) {
		return src.read({static_cast<std::byte*>(dest), static_cast<size_t>(num)});
	};

	uint32_t bytePos = src.pos();

	Error error = Error::NONE;

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
		if (!f.byteDepth.has_value()) {
			// Haven't found the "fmt " tag yet, so we don't know the bit depth. Shouldn't happen.
			return Error::FILE_UNSUPPORTED;
		}
		if (f.numChannels != 1) {
			return Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO; // Stereo files are never usable.
		}
		// If this isn't actually a wavetable-specifying file (nor a wavetable-looking length), and the
		// user isn't insisting, opt out.
		if (!f.fileExplicitlySpecifiesSelfAsWaveTable && !makeWaveTableWorkAtAllCosts) {
			const int32_t audioDataLengthSamples = f.audioDataLengthBytes / *f.byteDepth;
			if ((audioDataLengthSamples & 2047) != 0) {
				return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
			}
		}
		return Error::NONE;
	};

	bool stop = false; // A mid-chunk read error abandons the scan and falls to the found-chunk check.

	while (bytePos < src.size() && !stop) {

		struct {
			uint32_t name;
			uint32_t length;
		} thisChunk{};

		error = readBytes(reinterpret_cast<char*>(&thisChunk), 4 * 2);
		if (error != Error::NONE) {
			break;
		}

		if (isAiff) {
			thisChunk.length = swapEndianness32(thisChunk.length);
		}

		const uint32_t bytesCurrentChunkNotRoundedUp = thisChunk.length;
		// If the chunk size is odd, skip the trailing padding byte too (a RIFF requirement).
		thisChunk.length = (thisChunk.length + 1) & ~static_cast<uint32_t>(1);

		const uint32_t bytePosOfThisChunkData = src.pos();
		bytePos = bytePosOfThisChunkData + thisChunk.length; // Where the next RIFF chunk begins.

		// ------ WAV ------
		if (!isAiff) {
			switch (thisChunk.name) {

			// Data chunk - "data"
			case charsToIntegerConstant('d', 'a', 't', 'a'): {
				f.foundDataChunk = true;
				f.audioDataStartPosBytes = bytePosOfThisChunkData;
				f.audioDataLengthBytes = bytesCurrentChunkNotRoundedUp;
				if (isWaveTable) {
					return finishWaveTable(); // Header done; the caller runs WaveTable::setup.
				}
				break;
			}

			// Format chunk - "fmt "
			case charsToIntegerConstant('f', 'm', 't', ' '): {
				f.foundFmtChunk = true;

				std::array<uint32_t, 4> header{};
				error = readBytes(reinterpret_cast<char*>(header.data()), 4 * 4);
				if (error != Error::NONE) {
					return error;
				}

				// Bit depth.
				const uint16_t bits = header[3] >> 16;
				switch (bits) {
				case 8:
					f.rawDataFormat = RawDataFormat::UNSIGNED_8;
					[[fallthrough]];
				case 16:
				case 24:
				case 32:
					f.byteDepth = bits >> 3;
					break;
				case 256 ... 65535:
					__builtin_unreachable();
				default:
					return Error::FILE_UNSUPPORTED;
				}

				// Format.
				const uint16_t format = header[0];
				if (format == WAV_FORMAT_PCM) {}
				else if (format == WAV_FORMAT_FLOAT && f.byteDepth == 4) {
					f.rawDataFormat = RawDataFormat::FLOAT;
				}
				else {
					return Error::FILE_UNSUPPORTED;
				}

				// Num channels.
				f.numChannels = header[0] >> 16;
				if (f.numChannels != 1 && f.numChannels != 2) {
					return Error::FILE_UNSUPPORTED;
				}

				if (isSample) {
					f.sampleRate = header[1];
					if (f.sampleRate < 5000 || f.sampleRate > 96000) {
						return Error::FILE_UNSUPPORTED;
					}
				}
				break;
			}

			// Sample chunk - "smpl"
			case charsToIntegerConstant('s', 'm', 'p', 'l'): {
				if (isSample) {
					std::array<uint32_t, 9> data{};
					error = readBytes(reinterpret_cast<char*>(data.data()), 4 * 9);
					if (error != Error::NONE) {
						break;
					}

					const uint32_t midiNote = data[3];
					const uint32_t midiPitchFraction = data[4];
					const uint32_t numLoops = data[7];

					if ((midiNote || midiPitchFraction) && midiNote < 128) {
						f.midiNote =
						    static_cast<float>(midiPitchFraction) / (static_cast<uint64_t>(1) << 32) + midiNote;
					}

					if (numLoops == 1) {
						for (uint32_t l = 0; l < numLoops; l++) {
							D_PRINTLN("loop  %d", l);

							std::array<uint32_t, 6> loopData{};
							error = readBytes(reinterpret_cast<char*>(loopData.data()), 4 * 6);
							if (error != Error::NONE) {
								stop = true;
								break;
							}

							f.fileLoopStartSamples = loopData[2];
							f.fileLoopEndSamples = loopData[3];
						}
					}
				}
				break;
			}

			// Instrument chunk - "inst"
			case charsToIntegerConstant('i', 'n', 's', 't'): {
				if (isSample) {
					std::array<uint8_t, 7> data{};
					error = readBytes(reinterpret_cast<char*>(data.data()), 7);
					if (error != Error::NONE) {
						break;
					}

					const uint8_t midiNote = data[0];
					const int8_t fineTune = data[1];
					if (midiNote < 128) {
						f.midiNote = static_cast<float>(midiNote) - static_cast<float>(fineTune) * 0.01;
					}
				}
				break;
			}

			// Serum wavetable chunk - "clm "
			case charsToIntegerConstant('c', 'l', 'm', ' '): {
				std::array<char, 7> data{};
				error = readBytes(data.data(), 7);
				if (error != Error::NONE) {
					break;
				}

				if ((*reinterpret_cast<uint32_t*>(data.data()) & 0x00FFFFFF)
				    == charsToIntegerConstant('<', '!', '>', 0)) {
					f.fileExplicitlySpecifiesSelfAsWaveTable = true;
					// The cycle-size digits follow the "<!>" marker. Use pointer arithmetic for the past-the-end
					// bound — &data[7] would index one past a 7-element array (UB; trips bounds-checked operator[]).
					const int32_t number = memToUIntOrError(data.data() + 3, data.data() + data.size());
					if (number >= 1) {
						f.waveTableCycleSize = number;
						D_PRINTLN("clm tag num samples per cycle:  %d", f.waveTableCycleSize);
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
				f.foundDataChunk = true;

				uint32_t offset = 0;
				error = readBytes(reinterpret_cast<char*>(&offset), 4);
				if (error != Error::NONE) {
					return error;
				}
				offset = swapEndianness32(offset);
				f.audioDataLengthBytes = bytesCurrentChunkNotRoundedUp - offset - 8;
				f.audioDataStartPosBytes = src.pos() + 4 + offset;

				if (isWaveTable) {
					return finishWaveTable();
				}
				break;
			}

			// COMM
			case charsToIntegerConstant('C', 'O', 'M', 'M'): {
				f.foundFmtChunk = true;

				if (thisChunk.length != 18) {
					return Error::FILE_UNSUPPORTED;
				}

				std::array<uint16_t, 9> header{};
				error = readBytes(reinterpret_cast<char*>(header.data()), 18);
				if (error != Error::NONE) {
					return error;
				}

				// Num channels.
				f.numChannels = swapEndianness2x16(header[0]);
				if (f.numChannels != 1 && f.numChannels != 2) {
					return Error::FILE_UNSUPPORTED;
				}

				// Bit depth.
				const uint16_t bits = swapEndianness2x16(header[3]);
				if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
					return Error::FILE_UNSUPPORTED;
				}
				f.byteDepth = bits >> 3;

				if (*f.byteDepth > 1) {
					f.rawDataFormat = static_cast<RawDataFormat>(util::to_underlying(RawDataFormat::ENDIANNESS_WRONG_16)
					                                             + *f.byteDepth - 2);
				}

				if (isSample) {
					const uint32_t sampleRate = ConvertFromIeeeExtended(reinterpret_cast<unsigned char*>(&header[4]));
					if (sampleRate < 5000 || sampleRate > 96000) {
						return Error::FILE_UNSUPPORTED;
					}
					f.sampleRate = sampleRate;
				}
				break;
			}

			// MARK
			case charsToIntegerConstant('M', 'A', 'R', 'K'): {
				error = readBytes(reinterpret_cast<char*>(&numMarkers), 2);
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
					error = readBytes(reinterpret_cast<char*>(&markerId), 2);
					if (error != Error::NONE) {
						stop = true;
						break;
					}
					markerIDs[m] = swapEndianness2x16(markerId);

					uint32_t markerPos = 0;
					error = readBytes(reinterpret_cast<char*>(&markerPos), 4);
					if (error != Error::NONE) {
						stop = true;
						break;
					}
					markerPositions[m] = swapEndianness32(markerPos);

					uint8_t stringLength = 0;
					error = readBytes(reinterpret_cast<char*>(&stringLength), 1);
					if (error != Error::NONE) {
						stop = true;
						break;
					}

					const uint32_t stringLengthRoundedUpToBeEven =
					    (static_cast<uint32_t>(stringLength) + 1) & ~static_cast<uint32_t>(1);
					// Skip the (even-padded) marker-name string; the next read fetches from here.
					src.seekForwardTo(src.pos() + stringLengthRoundedUpToBeEven);
				}
				break;
			}

			// INST
			case charsToIntegerConstant('I', 'N', 'S', 'T'): {
				if (isSample) {
					std::array<uint8_t, 8> data{};
					error = readBytes(reinterpret_cast<char*>(data.data()), 8);
					if (error != Error::NONE) {
						break;
					}

					const uint8_t midiNote = data[0];
					const int8_t fineTune = data[1];
					if ((midiNote || fineTune) && midiNote < 128) {
						f.midiNote = static_cast<float>(midiNote) - static_cast<float>(fineTune) * 0.01;
						D_PRINTLN("unshifted note:  %s", *f.midiNote);
					}

					// Just read the sustain loop, which comes first.
					std::array<uint16_t, 3> loopData{};
					error = readBytes(reinterpret_cast<char*>(loopData.data()), 3 * 2);
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
			src.seekForwardTo(bytePos);
		}
	}

	if (!f.foundDataChunk || !f.foundFmtChunk) {
		return Error::FILE_CORRUPTED;
	}

	// Sample-only finalisation (a wavetable returned from the data/SSND chunk above).
	if (isSample && isAiff && sustainLoopEndMarkerId != -1) {
		for (int32_t m = 0; m < numMarkers; m++) {
			if (markerIDs[m] == sustainLoopBeginMarkerId) {
				f.fileLoopStartSamples = markerPositions[m];
			}
			if (markerIDs[m] == sustainLoopEndMarkerId) {
				f.fileLoopEndSamples = markerPositions[m];
			}
		}
	}

	return Error::NONE;
}
} // namespace

std::expected<SampleHeader, Error> parseSampleHeader(AudioByteSource& src) {
	const std::expected<Container, Error> container = detectContainer(src);
	if (!container) {
		return std::unexpected(container.error());
	}

	HeaderFields f;
	if (const Error error = walkChunks(src, *container, ParseMode::Sample, /*makeWaveTableWorkAtAllCosts=*/false, f);
	    error != Error::NONE) {
		return std::unexpected(error);
	}

	// A successful Sample parse always saw a "fmt "/"COMM" chunk, so byteDepth/numChannels are set.
	return SampleHeader{
	    .numChannels = *f.numChannels,
	    .byteDepth = *f.byteDepth,
	    .rawDataFormat = f.rawDataFormat,
	    .sampleRate = f.sampleRate,
	    .audioDataStartPosBytes = f.audioDataStartPosBytes,
	    .audioDataLengthBytes = f.audioDataLengthBytes,
	    .fileLoopStartSamples = f.fileLoopStartSamples,
	    .fileLoopEndSamples = f.fileLoopEndSamples,
	    .midiNote = f.midiNote,
	    .waveTableCycleSize = f.waveTableCycleSize,
	    .fileExplicitlySpecifiesSelfAsWaveTable = f.fileExplicitlySpecifiesSelfAsWaveTable,
	};
}

std::expected<WaveTableHeader, Error> parseWaveTableHeader(AudioByteSource& src, bool makeWaveTableWorkAtAllCosts) {
	const std::expected<Container, Error> container = detectContainer(src);
	if (!container) {
		return std::unexpected(container.error());
	}
	// AIFF files will only be used for WaveTables if the user insists.
	if (*container == Container::Aiff && !makeWaveTableWorkAtAllCosts) {
		return std::unexpected(Error::FILE_NOT_LOADABLE_AS_WAVETABLE);
	}

	HeaderFields f;
	if (const Error error = walkChunks(src, *container, ParseMode::WaveTable, makeWaveTableWorkAtAllCosts, f);
	    error != Error::NONE) {
		return std::unexpected(error);
	}

	// walkChunks only returns NONE for a WaveTable once finishWaveTable() has validated the located data, so
	// byteDepth/numChannels are set.
	return WaveTableHeader{
	    .numChannels = *f.numChannels,
	    .byteDepth = *f.byteDepth,
	    .rawDataFormat = f.rawDataFormat,
	    .audioDataStartPosBytes = f.audioDataStartPosBytes,
	    .audioDataLengthBytes = f.audioDataLengthBytes,
	    .cycleSize = f.waveTableCycleSize,
	};
}
