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
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/audio_file_reader.h"
#include "storage/wave_table/wave_table.h"
#include "util/misc.h"
#include <cstring>

#define MAX_NUM_MARKERS 8

Error AudioFile::loadFile(AudioFileReader* reader, bool isAiff, bool makeWaveTableWorkAtAllCosts) {

	// AIFF files will only be used for WaveTables if the user insists
	if (type == AudioFileType::WAVETABLE && !makeWaveTableWorkAtAllCosts && isAiff) {
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
	}

	// http://muratnkonar.com/aiff/

	// https://sites.google.com/site/musicgapi/technical-documents/wav-file-format?fbclid=IwAR1AWDhXq4m4LAlz991je_-NpRgRnD7eNg36ZfJ42X0xHkn38u5_skTvxHM#wavefilechunks

	uint32_t bytePos = reader->getBytePos();

	Error error;
	bool foundDataChunk = false; // Also applies to AIFF file's SSND chunk
	bool foundFmtChunk = false;  // Also applies to AIFF file's COMM chunk
	bool fileExplicitlySpecifiesSelfAsWaveTable = false;
	uint8_t byteDepth = 255; // 255 means no "fmt " or "COMM" chunk seen yet.
	RawDataFormat rawDataFormat = RawDataFormat::NATIVE;
	uint32_t audioDataStartPosBytes;
	uint32_t audioDataLengthBytes;
	uint32_t waveTableCycleSize = 2048;

	// This stuff for AIFF files only
	int16_t sustainLoopBeginMarkerId = -1;
	int16_t sustainLoopEndMarkerId = -1;
	uint16_t numMarkers;
	int16_t markerIDs[MAX_NUM_MARKERS];
	uint32_t markerPositions[MAX_NUM_MARKERS];

	while (bytePos < reader->fileSize) {

		struct {
			uint32_t name;
			uint32_t length;
		} thisChunk;

		error = reader->readBytes((char*)&thisChunk, 4 * 2);
		if (error != Error::NONE) {
			break;
		}

		if (isAiff) {
			thisChunk.length = swapEndianness32(thisChunk.length);
		}

		uint32_t bytesCurrentChunkNotRoundedUp = thisChunk.length;
		thisChunk.length =
		    (thisChunk.length + 1) & ~(uint32_t)1; // If chunk size is odd, skip the extra byte of padding at the end
		                                           // too. Weird RIFF file requirement.

		uint32_t bytePosOfThisChunkData = reader->getBytePos();

		// Move on for next RIFF chunk
		bytePos = bytePosOfThisChunkData + thisChunk.length;

		// ------ WAV ------
		if (!isAiff) {

			switch (thisChunk.name) {

			// Data chunk - "data"
			case charsToIntegerConstant('d', 'a', 't', 'a'): {
				foundDataChunk = true;
				audioDataStartPosBytes = bytePosOfThisChunkData;
				audioDataLengthBytes = bytesCurrentChunkNotRoundedUp;
				if (type == AudioFileType::WAVETABLE) {
doSetupWaveTable:
					if (byteDepth == 255) {
						return Error::FILE_UNSUPPORTED; // If haven't found "fmt " tag yet, we don't know the bit depth
						                                // or anything. Shouldn't happen.
					}

					if (numChannels != 1) {
						return Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO; // Stereo files not useable as
						                                                             // WaveTable, ever.
					}

					// If this isn't actually a wavetable-specifying file or at least a wavetable-looking length, and
					// the user isn't insisting, then opt not to do it.
					if (!fileExplicitlySpecifiesSelfAsWaveTable && !makeWaveTableWorkAtAllCosts) {
						int32_t audioDataLengthSamples = audioDataLengthBytes / byteDepth;
						if (audioDataLengthSamples & 2047) {
							return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
						}
					}
					error = ((WaveTable*)this)
					            ->setup(nullptr, waveTableCycleSize, audioDataStartPosBytes, audioDataLengthBytes,
					                    byteDepth, rawDataFormat, (WaveTableReader*)reader);
					if (true || error != Error::NONE) {
						return error; // Just always return here, for now.
					}
				}
				break;
			}

			// Format chunk - "fmt "
			case charsToIntegerConstant('f', 'm', 't', ' '): {
				foundFmtChunk = true;

				// Read and process fmt chunk
				uint32_t header[4];
				error = reader->readBytes((char*)&header, 4 * 4);
				if (error != Error::NONE) {
					return error;
				}

				// Bit depth
				uint16_t bits = header[3] >> 16;
				switch (bits) {
				case 8:
					rawDataFormat = RawDataFormat::UNSIGNED_8;
					// No break
				case 16:
				case 24:
				case 32:
					byteDepth = bits >> 3;
					break;
				case 256 ... 65535:
					__builtin_unreachable();
				default:
					return Error::FILE_UNSUPPORTED;
				}

				// Format
				uint16_t format = header[0];
				if (format == WAV_FORMAT_PCM) {}
				else if (format == WAV_FORMAT_FLOAT && byteDepth == 4) {
					rawDataFormat = RawDataFormat::FLOAT;
				}
				else {
					return Error::FILE_UNSUPPORTED;
				}

				// Num channels
				numChannels = header[0] >> 16;
				if (numChannels != 1 && numChannels != 2) {
					return Error::FILE_UNSUPPORTED;
				}

				if (type == AudioFileType::SAMPLE) {
					((Sample*)this)->byteDepth = byteDepth;
					((Sample*)this)->rawDataFormat = rawDataFormat;

					// Sample rate
					((Sample*)this)->sampleRate = header[1];
					if (((Sample*)this)->sampleRate < 5000 || ((Sample*)this)->sampleRate > 96000) {
						return Error::FILE_UNSUPPORTED;
					}
				}

				break;
			}

			// Sample chunk - "smpl"
			case charsToIntegerConstant('s', 'm', 'p', 'l'): {
				if (type == AudioFileType::SAMPLE) {

					uint32_t data[9];
					error = reader->readBytes((char*)data, 4 * 9);
					if (error != Error::NONE) {
						break;
					}

					uint32_t midiNote = data[3];
					uint32_t midiPitchFraction = data[4];
					uint32_t numLoops = data[7];

					if ((midiNote || midiPitchFraction) && midiNote < 128) {
						((Sample*)this)->midiNoteFromFile = (float)midiPitchFraction / ((uint64_t)1 << 32) + midiNote;
					}

					/*
					D_PRINTLN("unity note:  %d", midiNote);

					D_PRINTLN("num loops:  %d", numLoops);
					*/

					if (numLoops == 1) {

						// Go through loops
						for (int32_t l = 0; l < numLoops; l++) {
							D_PRINTLN("loop  %d", l);

							uint32_t loopData[6];
							error = reader->readBytes((char*)loopData, 4 * 6);
							if (error != Error::NONE) {
								goto finishedWhileLoop;
							}

							D_PRINTLN("start:  %d", loopData[2]);
							((Sample*)this)->fileLoopStartSamples = loopData[2];

							D_PRINTLN("end:  %d", loopData[3]);
							((Sample*)this)->fileLoopEndSamples = loopData[3];

							D_PRINTLN("play count:  %d", loopData[5]);
						}
					}
				}
				break;
			}

			// Instrument chunk - "inst"
			case charsToIntegerConstant('i', 'n', 's', 't'): {
				if (type == AudioFileType::SAMPLE) {

					uint8_t data[7];
					error = reader->readBytes((char*)data, 7);
					if (error != Error::NONE) {
						break;
					}

					uint8_t midiNote = data[0];
					int8_t fineTune = data[1];
					if (midiNote < 128) {
						((Sample*)this)->midiNoteFromFile = (float)midiNote - (float)fineTune * 0.01;
					}

					D_PRINTLN("unshifted note:  %d", midiNote);
				}
				break;
			}

			// Serum wavetable chunk - "clm "
			case charsToIntegerConstant('c', 'l', 'm', ' '): {
				char data[7];
				error = reader->readBytes((char*)data, 7);
				if (error != Error::NONE) {
					break;
				}

				if ((*(uint32_t*)data & 0x00FFFFFF) == charsToIntegerConstant('<', '!', '>', 0)) {
					fileExplicitlySpecifiesSelfAsWaveTable = true;
					int32_t number = memToUIntOrError(&data[3], &data[7]);

					if (number >= 1) {
						waveTableCycleSize = number;
						D_PRINTLN("clm tag num samples per cycle:  %d", waveTableCycleSize);
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

				// Offset
				uint32_t offset;
				error = reader->readBytes((char*)&offset, 4);
				if (error != Error::NONE) {
					return error;
				}
				offset = swapEndianness32(offset);
				audioDataLengthBytes = bytesCurrentChunkNotRoundedUp - offset - 8;

				// If we're here, we found the data! Take note of where it starts
				audioDataStartPosBytes = reader->getBytePos() + 4 + offset;

				if (type == AudioFileType::WAVETABLE) {
					goto doSetupWaveTable;
				}
				break;
			}

			// COMM
			case charsToIntegerConstant('C', 'O', 'M', 'M'): {
				foundFmtChunk = true;

				if (thisChunk.length != 18) {
					return Error::FILE_UNSUPPORTED; // Why'd I do this?
				}

				// Read and process COMM chunk
				uint16_t header[9];
				error = reader->readBytes((char*)header, 18);
				if (error != Error::NONE) {
					return error;
				}

				// Num channels
				numChannels = swapEndianness2x16(header[0]);
				if (numChannels != 1 && numChannels != 2) {
					return Error::FILE_UNSUPPORTED;
				}

				// Bit depth
				uint16_t bits = swapEndianness2x16(header[3]);

				if (bits == 8 || bits == 16 || bits == 24 || bits == 32) {}
				else {
					return Error::FILE_UNSUPPORTED;
				}
				byteDepth = bits >> 3;

				if (byteDepth > 1) {
					rawDataFormat = static_cast<RawDataFormat>(util::to_underlying(RawDataFormat::ENDIANNESS_WRONG_16)
					                                           + byteDepth - 2);
				}

				if (type == AudioFileType::SAMPLE) {
					((Sample*)this)->byteDepth = byteDepth;

					// Sample rate
					uint32_t sampleRate = ConvertFromIeeeExtended((unsigned char*)&header[4]);
					if (sampleRate < 5000 || sampleRate > 96000) {
						return Error::FILE_UNSUPPORTED;
					}

					((Sample*)this)->sampleRate = sampleRate;
				}
				break;
			}

			// MARK
			case charsToIntegerConstant('M', 'A', 'R', 'K'): {
				error = reader->readBytes((char*)&numMarkers, 2);
				if (error != Error::NONE) {
					break;
				}
				numMarkers = swapEndianness2x16(numMarkers);

				D_PRINTLN("numMarkers:  %d", numMarkers);

				if (numMarkers > MAX_NUM_MARKERS) {
					numMarkers = MAX_NUM_MARKERS;
				}

				for (int32_t m = 0; m < numMarkers; m++) {
					uint16_t markerId;
					error = reader->readBytes((char*)&markerId, 2);
					if (error != Error::NONE) {
						goto finishedWhileLoop;
					}
					markerIDs[m] = swapEndianness2x16(markerId);

					D_PRINTLN("");
					D_PRINTLN("markerId:  %d", markerIDs[m]);

					uint32_t markerPos;
					error = reader->readBytes((char*)&markerPos, 4);
					if (error != Error::NONE) {
						goto finishedWhileLoop;
					}
					markerPositions[m] = swapEndianness32(markerPos);

					D_PRINTLN("markerPos:  %d", markerPositions[m]);

					uint8_t stringLength;
					error = reader->readBytes((char*)&stringLength, 1);
					if (error != Error::NONE) {
						goto finishedWhileLoop;
					}

					uint32_t stringLengthRoundedUpToBeEven = ((uint32_t)stringLength + 1) & ~(uint32_t)1;
					reader->byteIndexWithinCluster +=
					    stringLengthRoundedUpToBeEven; // Cluster boundaries will be checked at next read
				}
				break;
			}

			// INST
			case charsToIntegerConstant('I', 'N', 'S', 'T'): {
				if (type == AudioFileType::SAMPLE) {
					uint8_t data[8];
					error = reader->readBytes((char*)data, 8);
					if (error != Error::NONE) {
						break;
					}

					uint8_t midiNote = data[0];
					int8_t fineTune = data[1];
					if ((midiNote || fineTune) && midiNote < 128) {
						((Sample*)this)->midiNoteFromFile = (float)midiNote - (float)fineTune * 0.01;
						D_PRINTLN("unshifted note:  %s", ((Sample*)this)->midiNoteFromFile);
					}

					// for (int32_t l = 0; l < 2; l++) {

					// if (l == 0) D_PRINTLN("sustain loop:");
					// else D_PRINTLN("release loop:");

					// Just read the sustain loop, which is first

					uint16_t loopData[3];
					error = reader->readBytes((char*)loopData, 3 * 2);
					if (error != Error::NONE) {
						break;
					}

					D_PRINTLN("play mode:  %d", swapEndianness2x16(loopData[0]));

					sustainLoopBeginMarkerId = swapEndianness2x16(loopData[1]);
					D_PRINTLN("begin marker id:  %d", sustainLoopBeginMarkerId);

					sustainLoopEndMarkerId = swapEndianness2x16(loopData[2]);
					D_PRINTLN("end marker id:  %d", sustainLoopEndMarkerId);
					//}
				}
				break;
			}
			}
		}

		reader->jumpForwardToBytePos(bytePos);
	}

finishedWhileLoop:

	if (!foundDataChunk || !foundFmtChunk) {
		return Error::FILE_CORRUPTED;
	}

	if (type == AudioFileType::SAMPLE) {

		if (isAiff) {
			((Sample*)this)->rawDataFormat = rawDataFormat;

			// Sort out the sustain loop
			if (sustainLoopEndMarkerId != -1) {
				for (int32_t m = 0; m < numMarkers; m++) {

					if (markerIDs[m] == sustainLoopBeginMarkerId) {
						((Sample*)this)->fileLoopStartSamples = markerPositions[m];
					}

					if (markerIDs[m] == sustainLoopEndMarkerId) {
						((Sample*)this)->fileLoopEndSamples = markerPositions[m];
					}
				}
			}
		}

		((Sample*)this)->audioDataStartPosBytes = audioDataStartPosBytes;
		((Sample*)this)->audioDataLengthBytes = audioDataLengthBytes;
		((Sample*)this)->waveTableCycleSize = waveTableCycleSize;
		((Sample*)this)->fileExplicitlySpecifiesSelfAsWaveTable = fileExplicitlySpecifiesSelfAsWaveTable;
	}

	return Error::NONE;
}

void AudioFile::addReason() {
	// If it was zero before, it's no longer unused
	if (!numReasonsToBeLoaded) {
		remove();
		numReasonsIncreasedFromZero();
	}

	numReasonsToBeLoaded++;
}

void AudioFile::removeReason(char const* errorCode) {

	numReasonsToBeLoaded--;

	// If it's now zero, it's become unused
	if (numReasonsToBeLoaded == 0) {
		numReasonsDecreasedToZero(errorCode);
		GeneralMemoryAllocator::get().putStealableInQueue(this, StealableQueue::NO_SONG_AUDIO_FILE_OBJECTS);
	}

	else if (numReasonsToBeLoaded < 0) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E004"); // Luc got this! And Paolo. (Must have been years ago :D)
#endif
		numReasonsToBeLoaded = 0; // Save it from crashing
	}
}

bool AudioFile::mayBeStolen(void* thingNotToStealFrom) {

	if (numReasonsToBeLoaded) {
		return false;
	}

	// If we were stolen, sampleManager.audioFiles would get an entry deleted from it, and that's not allowed while it's
	// being inserted to, which is when we'd be provided it as the thingNotToStealFrom.
	return (thingNotToStealFrom != &audioFileManager.audioFiles);

	// We don't have to worry about e.g. a Sample being stolen as we try to allocate a Cluster for it in the same way as
	// we do with SampleCaches - because in a case like this, the Sample would have a reason and so not be stealable.
}

void AudioFile::steal(char const* errorCode) {
	// The destructor is about to be called too, so we don't have to do too much.

	int32_t i = audioFileManager.audioFiles.searchForExactObject(this);
	if (i < 0) {
#if ALPHA_OR_BETA_VERSION
		display->displayPopup(errorCode); // Jensg still getting.
#endif
	}
	else {
		audioFileManager.audioFiles.removeElement(i);
	}
}

StealableQueue AudioFile::getAppropriateQueue() {
	return StealableQueue::NO_SONG_AUDIO_FILE_OBJECTS;
}
