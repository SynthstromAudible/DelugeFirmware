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

#include <AudioFile.h>
#include <AudioFileManager.h>
#include "functions.h"
#include <string.h>
#include "uart.h"
#include "AudioFileReader.h"
#include "Sample.h"
#include "WaveTable.h"
#include "numericdriver.h"
#include "GeneralMemoryAllocator.h"

extern "C" {
#include "uart_all_cpus.h"
}

AudioFile::AudioFile(int newType) : type(newType) {
	numReasonsToBeLoaded = 0;
}

AudioFile::~AudioFile() {
}

#define MAX_NUM_MARKERS 8

int AudioFile::loadFile(AudioFileReader* reader, bool isAiff, bool makeWaveTableWorkAtAllCosts) {

	// AIFF files will only be used for WaveTables if the user insists
	if (type == AUDIO_FILE_TYPE_WAVETABLE && !makeWaveTableWorkAtAllCosts && isAiff)
		return ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE;

	// http://muratnkonar.com/aiff/

	// https://sites.google.com/site/musicgapi/technical-documents/wav-file-format?fbclid=IwAR1AWDhXq4m4LAlz991je_-NpRgRnD7eNg36ZfJ42X0xHkn38u5_skTvxHM#wavefilechunks

	uint32_t bytePos = reader->getBytePos();

	int error;
	bool foundDataChunk = false; // Also applies to AIFF file's SSND chunk
	bool foundFmtChunk = false;  // Also applies to AIFF file's COMM chunk
	bool fileExplicitlySpecifiesSelfAsWaveTable = false;
	uint8_t byteDepth = 255; // 255 means no "fmt " or "COMM" chunk seen yet.
	uint8_t rawDataFormat = RAW_DATA_FINE;
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
		if (error) break;

		if (isAiff) thisChunk.length = swapEndianness32(thisChunk.length);

		uint32_t bytesCurrentChunkNotRoundedUp = thisChunk.length;
		thisChunk.length =
		    (thisChunk.length + 1)
		    & ~(uint32_t)1; // If chunk size is odd, skip the extra byte of padding at the end too. Weird RIFF file requirement.

		uint32_t bytePosOfThisChunkData = reader->getBytePos();

		// Move on for next RIFF chunk
		bytePos = bytePosOfThisChunkData + thisChunk.length;

		// ------ WAV ------
		if (!isAiff) {

			switch (thisChunk.name) {

			// Data chunk - "data"
			case 'atad': {
				foundDataChunk = true;
				audioDataStartPosBytes = bytePosOfThisChunkData;
				audioDataLengthBytes = bytesCurrentChunkNotRoundedUp;
				if (type == AUDIO_FILE_TYPE_WAVETABLE) {
doSetupWaveTable:
					if (byteDepth == 255)
						return ERROR_FILE_UNSUPPORTED; // If haven't found "fmt " tag yet, we don't know the bit depth or anything. Shouldn't happen.

					if (numChannels != 1)
						return ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO; // Stereo files not useable as WaveTable, ever.

					// If this isn't actually a wavetable-specifying file or at least a wavetable-looking length, and the user isn't insisting, then opt not to do it.
					if (!fileExplicitlySpecifiesSelfAsWaveTable && !makeWaveTableWorkAtAllCosts) {
						int audioDataLengthSamples = audioDataLengthBytes / byteDepth;
						if (audioDataLengthSamples & 2047) return ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE;
					}
					error = ((WaveTable*)this)
					            ->setup(NULL, waveTableCycleSize, audioDataStartPosBytes, audioDataLengthBytes,
					                    byteDepth, rawDataFormat, (WaveTableReader*)reader);
					if (true || error) return error; // Just always return here, for now.
				}
				break;
			}

			// Format chunk - "fmt "
			case ' tmf': {
				foundFmtChunk = true;

				// Read and process fmt chunk
				uint32_t header[4];
				error = reader->readBytes((char*)&header, 4 * 4);
				if (error) return error;

				// Bit depth
				uint16_t bits = header[3] >> 16;
				switch (bits) {
				case 8:
					rawDataFormat = RAW_DATA_UNSIGNED_8;
					// No break
				case 16:
				case 24:
				case 32:
					byteDepth = bits >> 3;
					break;
				case 256 ... 65535:
					__builtin_unreachable();
				default:
					return ERROR_FILE_UNSUPPORTED;
				}

				// Format
				uint16_t format = header[0];
				if (format == WAV_FORMAT_PCM) {}
				else if (format == WAV_FORMAT_FLOAT && byteDepth == 4) {
					rawDataFormat = RAW_DATA_FLOAT;
				}
				else {
					return ERROR_FILE_UNSUPPORTED;
				}

				// Num channels
				numChannels = header[0] >> 16;
				if (numChannels != 1 && numChannels != 2) {
					return ERROR_FILE_UNSUPPORTED;
				}

				if (type == AUDIO_FILE_TYPE_SAMPLE) {
					((Sample*)this)->byteDepth = byteDepth;
					((Sample*)this)->rawDataFormat = rawDataFormat;

					// Sample rate
					((Sample*)this)->sampleRate = header[1];
					if (((Sample*)this)->sampleRate < 5000 || ((Sample*)this)->sampleRate > 96000) {
						return ERROR_FILE_UNSUPPORTED;
					}
				}

				break;
			}

			// Sample chunk - "smpl"
			case 'lpms': {
				if (type == AUDIO_FILE_TYPE_SAMPLE) {

					uint32_t data[9];
					error = reader->readBytes((char*)data, 4 * 9);
					if (error) break;

					uint32_t midiNote = data[3];
					uint32_t midiPitchFraction = data[4];
					uint32_t numLoops = data[7];

					if ((midiNote || midiPitchFraction) && midiNote < 128)
						((Sample*)this)->midiNoteFromFile = (float)midiPitchFraction / ((uint64_t)1 << 32) + midiNote;

					/*
					Uart::print("unity note: ");
					Uart::println(midiNote);

					Uart::print("num loops: ");
					Uart::println(numLoops);
					*/

					if (numLoops == 1) {

						// Go through loops
						for (int l = 0; l < numLoops; l++) {
							//Uart::print("loop ");
							//Uart::println(l);

							uint32_t loopData[6];
							error = reader->readBytes((char*)loopData, 4 * 6);
							if (error) goto finishedWhileLoop;

							//Uart::print("start: ");
							//Uart::println(loopData[2]);
							((Sample*)this)->fileLoopStartSamples = loopData[2];

							//Uart::print("end: ");
							//Uart::println(loopData[3]);
							((Sample*)this)->fileLoopEndSamples = loopData[3];

							//Uart::print("play count: ");
							//Uart::println(loopData[5]);
						}
					}

					Uart::println("");
				}
				break;
			}

			// Instrument chunk - "inst"
			case 'tsni': {
				if (type == AUDIO_FILE_TYPE_SAMPLE) {

					uint8_t data[7];
					error = reader->readBytes((char*)data, 7);
					if (error) break;

					uint8_t midiNote = data[0];
					int8_t fineTune = data[1];
					if (midiNote < 128) ((Sample*)this)->midiNoteFromFile = (float)midiNote - (float)fineTune * 0.01;

					Uart::print("unshifted note: ");
					Uart::println(midiNote);
				}
				break;
			}

			// Serum wavetable chunk - "clm "
			case ' mlc': {
				char data[6];
				error = reader->readBytes((char*)data, 7);
				if (error) break;

				if ((*(uint32_t*)data & 0x00FFFFFF) == '>!<') {
					fileExplicitlySpecifiesSelfAsWaveTable = true;
					int number = memToUIntOrError(&data[3], &data[7]);

					if (number >= 1) {
						waveTableCycleSize = number;
						//uartPrint("clm tag num samples per cycle: ");
						//uartPrintNumber(waveTableNumSamplesPerCycle);
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
			case 'DNSS': {
				foundDataChunk = true;

				// Offset
				uint32_t offset;
				error = reader->readBytes((char*)&offset, 4);
				if (error) return error;
				offset = swapEndianness32(offset);
				audioDataLengthBytes = bytesCurrentChunkNotRoundedUp - offset - 8;

				// If we're here, we found the data! Take note of where it starts
				audioDataStartPosBytes = reader->getBytePos() + 4 + offset;

				if (type == AUDIO_FILE_TYPE_WAVETABLE) goto doSetupWaveTable;
				break;
			}

			// COMM
			case 'MMOC': {
				foundFmtChunk = true;

				if (thisChunk.length != 18) return ERROR_FILE_UNSUPPORTED; // Why'd I do this?

				// Read and process COMM chunk
				uint16_t header[9];
				error = reader->readBytes((char*)header, 18);
				if (error) return error;

				// Num channels
				numChannels = swapEndianness2x16(header[0]);
				if (numChannels != 1 && numChannels != 2) {
					return ERROR_FILE_UNSUPPORTED;
				}

				// Bit depth
				uint16_t bits = swapEndianness2x16(header[3]);

				if (bits == 8 || bits == 16 || bits == 24 || bits == 32) {}
				else return ERROR_FILE_UNSUPPORTED;
				byteDepth = bits >> 3;

				if (byteDepth > 1) {
					rawDataFormat = RAW_DATA_ENDIANNESS_WRONG_16 + byteDepth - 2;
				}

				if (type == AUDIO_FILE_TYPE_SAMPLE) {
					((Sample*)this)->byteDepth = byteDepth;

					// Sample rate
					uint32_t sampleRate = ConvertFromIeeeExtended((unsigned char*)&header[4]);
					if (sampleRate < 5000 || sampleRate > 96000) {
						return ERROR_FILE_UNSUPPORTED;
					}

					((Sample*)this)->sampleRate = sampleRate;
				}
				break;
			}

			// MARK
			case 'KRAM': {
				error = reader->readBytes((char*)&numMarkers, 2);
				if (error) break;
				numMarkers = swapEndianness2x16(numMarkers);

				Uart::print("numMarkers: ");
				Uart::println(numMarkers);

				if (numMarkers > MAX_NUM_MARKERS) numMarkers = MAX_NUM_MARKERS;

				for (int m = 0; m < numMarkers; m++) {
					uint16_t markerId;
					error = reader->readBytes((char*)&markerId, 2);
					if (error) goto finishedWhileLoop;
					markerIDs[m] = swapEndianness2x16(markerId);

					Uart::println("");
					Uart::print("markerId: ");
					Uart::println(markerIDs[m]);

					uint32_t markerPos;
					error = reader->readBytes((char*)&markerPos, 4);
					if (error) goto finishedWhileLoop;
					markerPositions[m] = swapEndianness32(markerPos);

					Uart::print("markerPos: ");
					Uart::println(markerPositions[m]);

					uint8_t stringLength;
					error = reader->readBytes((char*)&stringLength, 1);
					if (error) goto finishedWhileLoop;

					uint32_t stringLengthRoundedUpToBeEven = ((uint32_t)stringLength + 1) & ~(uint32_t)1;
					reader->byteIndexWithinCluster +=
					    stringLengthRoundedUpToBeEven; // Cluster boundaries will be checked at next read
				}
				break;
			}

			// INST
			case 'TSNI': {
				if (type == AUDIO_FILE_TYPE_SAMPLE) {
					uint8_t data[8];
					error = reader->readBytes((char*)data, 8);
					if (error) break;

					uint8_t midiNote = data[0];
					int8_t fineTune = data[1];
					if ((midiNote || fineTune) && midiNote < 128) {
						((Sample*)this)->midiNoteFromFile = (float)midiNote - (float)fineTune * 0.01;
						//Uart::print("unshifted note: ");
						//Uart::printlnfloat(newSample->midiNoteFromFile);
					}

					//for (int l = 0; l < 2; l++) {

					//if (l == 0) Uart::println("sustain loop:");
					//else Uart::println("release loop:");

					// Just read the sustain loop, which is first

					uint16_t loopData[3];
					error = reader->readBytes((char*)loopData, 3 * 2);
					if (error) break;

					//Uart::print("play mode: ");
					//Uart::println(swapEndianness2x16(loopData[0]));

					sustainLoopBeginMarkerId = swapEndianness2x16(loopData[1]);
					//Uart::print("begin marker id: ");
					//Uart::println(sustainLoopBeginMarkerId);

					sustainLoopEndMarkerId = swapEndianness2x16(loopData[2]);
					//Uart::print("end marker id: ");
					//Uart::println(sustainLoopEndMarkerId);
					//}
				}
				break;
			}
			}
		}

		reader->jumpForwardToBytePos(bytePos);
	}

finishedWhileLoop:

	if (!foundDataChunk || !foundFmtChunk) return ERROR_FILE_CORRUPTED;

	if (type == AUDIO_FILE_TYPE_SAMPLE) {

		if (isAiff) {
			((Sample*)this)->rawDataFormat = rawDataFormat;

			// Sort out the sustain loop
			if (sustainLoopEndMarkerId != -1) {
				for (int m = 0; m < numMarkers; m++) {

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

	return NO_ERROR;
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
		generalMemoryAllocator.putStealableInQueue(this, STEALABLE_QUEUE_NO_SONG_AUDIO_FILE_OBJECTS);
	}

	else if (numReasonsToBeLoaded < 0) {
#if ALPHA_OR_BETA_VERSION
		numericDriver.freezeWithError("E004"); // Luc got this! And Paolo. (Must have been years ago :D)
#endif
		numReasonsToBeLoaded = 0; // Save it from crashing
	}
}

bool AudioFile::mayBeStolen(void* thingNotToStealFrom) {

	if (numReasonsToBeLoaded) return false;

	// If we were stolen, sampleManager.audioFiles would get an entry deleted from it, and that's not allowed while it's being inserted to, which is when we'd be provided it as the thingNotToStealFrom.
	return (thingNotToStealFrom != &audioFileManager.audioFiles);

	// We don't have to worry about e.g. a Sample being stolen as we try to allocate a Cluster for it in the same way as we do with SampleCaches - because in a case like this, the Sample would have a reason and so not be stealable.
}

void AudioFile::steal(char const* errorCode) {
	// The destructor is about to be called too, so we don't have to do too much.

	int i = audioFileManager.audioFiles.searchForExactObject(this);
	if (i < 0) {
#if ALPHA_OR_BETA_VERSION
		numericDriver.displayPopup(errorCode); // Jensg still getting.
#endif
	}
	else {
		audioFileManager.audioFiles.removeElement(i);
	}
}

int AudioFile::getAppropriateQueue() {
	return STEALABLE_QUEUE_NO_SONG_AUDIO_FILE_OBJECTS;
}
