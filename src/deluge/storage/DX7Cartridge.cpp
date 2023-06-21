/**
 *
 * Copyright (c) 2014-2017 Pascal Gauthier.
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

#include "DX7Cartridge.h"

#include <cmath>
using namespace ::std;

uint8_t sysexChecksum(const uint8_t* sysex, int size) {
	int sum = 0;
	int i;

	for (i = 0; i < size; sum -= sysex[i++])
		;
	return sum & 0x7F;
}

void exportSysexPgm(uint8_t* dest, uint8_t* src) {
	uint8_t header[] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};

	memcpy(dest, header, 6);

	// copy 1 unpacked voices
	memcpy(dest + 6, src, 155);

	// make checksum for dump
	uint8_t footer[] = {sysexChecksum(src, 155), 0xF7};

	memcpy(dest + 161, footer, 2);
}

/**
 * Pack a program into a 32 packed sysex
 */
void DX7Cartridge::packProgram(uint8_t* src, int idx, char* name, char* opSwitch) {
	uint8_t* bulk = voiceData + 6 + (idx * 128);

	for (int op = 0; op < 6; op++) {
		// eg rate and level, brk pt, depth, scaling
		memcpy(bulk + op * 17, src + op * 21, 11);
		int pp = op * 17;
		int up = op * 21;

		// left curves
		bulk[pp + 11] = (src[up + 11] & 0x03) | ((src[up + 12] & 0x03) << 2);
		bulk[pp + 12] = (src[up + 13] & 0x07) | ((src[up + 20] & 0x0f) << 3);
		// kvs_ams
		bulk[pp + 13] = (src[up + 14] & 0x03) | ((src[up + 15] & 0x07) << 2);
		// output lvl
		if (opSwitch[op] == '0')
			bulk[pp + 14] = 0;
		else
			bulk[pp + 14] = src[up + 16];
		// fcoarse_mode
		bulk[pp + 15] = (src[up + 17] & 0x01) | ((src[up + 18] & 0x1f) << 1);
		// fine freq
		bulk[pp + 16] = src[up + 19];
	}
	memcpy(bulk + 102, src + 126, 9); // pitch env, algo
	bulk[111] = (src[135] & 0x07) | ((src[136] & 0x01) << 3);
	memcpy(bulk + 112, src + 137, 4); // lfo
	bulk[116] = (src[141] & 0x01) | (((src[142] & 0x07) << 1) | ((src[143] & 0x07) << 4));
	bulk[117] = src[144];

	int eos = 0;

	for (int i = 0; i < 10; i++) {
		char c = (char)name[i];
		if (c == 0)
			eos = 1;
		if (eos) {
			bulk[118 + i] = ' ';
			continue;
		}
		c = c < 32 ? ' ' : c;
		c = c > 127 ? ' ' : c;
		bulk[118 + i] = c;
	}
}

/**
 * This function normalize data that comes from corrupted sysex.
 * It used to avoid engine crashing upon extreme values
 */
char normparm(char value, char max, int id) {
	if (value <= max && value >= 0)
		return value;

	// if this is beyond the max, we expect a 0-255 range, normalize this
	// to the expected return value; and this value as a random data.

	value = abs(value);

	char v = ((float)value) / 255 * max;

	return v;
}

void DX7Cartridge::unpackProgram(uint8_t* unpackPgm, int idx) {
	// TODO put this in uint8_t :D
	char* bulk = (char*)voiceData + 6 + (idx * 128);

	for (int op = 0; op < 6; op++) {
		// eg rate and level, brk pt, depth, scaling

		for (int i = 0; i < 11; i++) {
			uint8_t currparm = bulk[op * 17 + i] & 0x7F; // mask BIT7 (don't care per sysex spec)
			unpackPgm[op * 21 + i] = normparm(currparm, 99, i);
		}

		memcpy(unpackPgm + op * 21, bulk + op * 17, 11);
		char leftrightcurves = bulk[op * 17 + 11] & 0xF; // bits 4-7 don't care per sysex spec
		unpackPgm[op * 21 + 11] = leftrightcurves & 3;
		unpackPgm[op * 21 + 12] = (leftrightcurves >> 2) & 3;
		char detune_rs = bulk[op * 17 + 12] & 0x7F;
		unpackPgm[op * 21 + 13] = detune_rs & 7;
		char kvs_ams = bulk[op * 17 + 13] & 0x1F; // bits 5-7 don't care per sysex spec
		unpackPgm[op * 21 + 14] = kvs_ams & 3;
		unpackPgm[op * 21 + 15] = (kvs_ams >> 2) & 7;
		unpackPgm[op * 21 + 16] = bulk[op * 17 + 14] & 0x7F; // output level
		char fcoarse_mode = bulk[op * 17 + 15] & 0x3F;       // bits 6-7 don't care per sysex spec
		unpackPgm[op * 21 + 17] = fcoarse_mode & 1;
		unpackPgm[op * 21 + 18] = (fcoarse_mode >> 1) & 0x1F;
		unpackPgm[op * 21 + 19] = bulk[op * 17 + 16] & 0x7F; // fine freq
		unpackPgm[op * 21 + 20] = (detune_rs >> 3) & 0x7F;
	}

	for (int i = 0; i < 8; i++) {
		uint8_t currparm = bulk[102 + i] & 0x7F; // mask BIT7 (don't care per sysex spec)
		unpackPgm[126 + i] = normparm(currparm, 99, 126 + i);
	}
	unpackPgm[134] = normparm(bulk[110] & 0x1F, 31, 134); // bits 5-7 are don't care per sysex spec

	char oks_fb = bulk[111] & 0xF; // bits 4-7 are don't care per spec
	unpackPgm[135] = oks_fb & 7;
	unpackPgm[136] = oks_fb >> 3;
	unpackPgm[137] = bulk[112] & 0x7F; // lfs
	unpackPgm[138] = bulk[113] & 0x7F; // lfd
	unpackPgm[139] = bulk[114] & 0x7F; // lpmd
	unpackPgm[140] = bulk[115] & 0x7F; // lamd
	char lpms_lfw_lks = bulk[116] & 0x7F;
	unpackPgm[141] = lpms_lfw_lks & 1;
	unpackPgm[142] = (lpms_lfw_lks >> 1) & 7;
	unpackPgm[143] = lpms_lfw_lks >> 4;
	unpackPgm[144] = bulk[117] & 0x7F;
	for (int name_idx = 0; name_idx < 10; name_idx++) {
		unpackPgm[145 + name_idx] = bulk[118 + name_idx] & 0x7F;
	} // name_idx
	  //    memcpy(unpackPgm + 144, bulk + 117, 11);  // transpose, name
}
