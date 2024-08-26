/**
 *
 * Copyright (c) 2014-2016 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 */

#ifndef PLUGINDATA_H_INCLUDED
#define PLUGINDATA_H_INCLUDED

#include "gui/l10n/strings.h"
#include <stdint.h>
#include <string.h>

uint8_t sysexChecksum(const uint8_t* sysex, int size);
void exportSysexPgm(uint8_t* dest, uint8_t* src);

#define SYSEX_HEADER {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00}
#define SYSEX_SIZE 4104
// single patch
#define SMALL_SYSEX_SIZE 163

class DX7Cartridge {
	uint8_t voiceData[SYSEX_SIZE];

	void setHeader() {
		uint8_t voiceHeader[] = SYSEX_HEADER;
		memcpy(voiceData, voiceHeader, 6);
		voiceData[4102] = sysexChecksum(voiceData + 6, 4096);
		voiceData[4103] = 0xF7;
	}

public:
	DX7Cartridge() {}

	DX7Cartridge(const DX7Cartridge& cpy) { memcpy(voiceData, cpy.voiceData, SYSEX_SIZE); }

	static void normalizePgmName(char buffer[11], const char* sysexName) {
		memcpy(buffer, sysexName, 10);

		for (int j = 0; j < 10; j++) {
			char c = (unsigned char)buffer[j];
			c &= 0x7F; // strip don't care most-significant bit from name
			switch (c) {
			case 92:
				c = 'Y';
				break; /* yen */
			case 126:
				c = '>';
				break; /* >> */
			case 127:
				c = '<';
				break; /* << */
			default:
				if (c < 32 || c > 127)
					c = 32;
				break;
			}
			buffer[j] = c;
		}
		buffer[10] = 0;

		// trim spaces at the end
		for (int j = 9; j >= 0; j--) {
			if (buffer[j] != 32) {
				break;
			}
			buffer[j] = 0;
		}
	}
	/**
	 * Loads sysex buffer
	 * Returns EMPTY_STRING if it was parsed successfully
	 * otherwise a string describing the error.
	 */
	deluge::l10n::String load(const uint8_t* stream, size_t size) {
		using deluge::l10n::String;
		const uint8_t* pos = stream;

		size_t minMsgSize = 163;

		if (size < minMsgSize) {
			memcpy(voiceData + 6, pos, size);
			return String::STRING_FOR_DX_ERROR_FILE_TOO_SMALL;
		}

		if (pos[0] != 0xF0) {
			// it is not, just copy the first 4096 bytes
			memcpy(voiceData + 6, pos, 4096);
			return String::STRING_FOR_DX_ERROR_NO_SYSEX_START;
		}

		int i;
		// check if this is the size of a DX7 sysex cartridge
		for (i = 0; i < size; i++) {
			if (pos[i] == 0xF7) {
				break;
			}
		}
		if (i == size) {
			return String::STRING_FOR_DX_ERROR_NO_SYSEX_END;
		}

		int msgSize = i + 1;

		if (msgSize != SYSEX_SIZE && msgSize != SMALL_SYSEX_SIZE) {
			return String::STRING_FOR_DX_ERROR_INVALID_LEN;
		}

		memcpy(voiceData, pos, msgSize);
		int dataSize = (msgSize == SYSEX_SIZE) ? 4096 : 155;
		if (sysexChecksum(voiceData + 6, dataSize) != pos[msgSize - 2]) {
			return String::STRING_FOR_DX_ERROR_CHECKSUM_FAIL;
		}

		if (voiceData[1] != 67 || (voiceData[3] != 9 && voiceData[3] != 0)) {
			return String::STRING_FOR_DX_ERROR_SYSEX_ID;
		}

		return String::EMPTY_STRING;
	}

	bool isCartridge() { return voiceData[3] == 9; }
	int numPatches() { return isCartridge() ? 32 : 1; }

	void saveVoice(uint8_t* sysex) {
		setHeader();
		memcpy(sysex, voiceData, SYSEX_SIZE);
	}

	char* getRawVoice() { return (char*)voiceData + 6; }

	void getProgramNames(char dest[32][11]) { // 10 chars + NUL
		for (int i = 0; i < numPatches(); i++) {
			getProgramName(i, dest[i]);
		}
	}

	void getProgramName(int32_t i, char dest[11]) {
		normalizePgmName(dest, getRawVoice() + ((i * 128) + (isCartridge() ? 118 : 145)));
	}

	void unpackProgram(uint8_t* unpackPgm, int idx);
	void packProgram(uint8_t* src, int idx, char* name, char* opSwitch);
};

#endif // PLUGINDATA_H_INCLUDED
