// Reverb model tuning values
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

const int32_t numcombs = 8;
const int32_t numallpasses = 4;
const float muted = 0;
const float fixedgain = 0.015f;
const float scalewet = 3;
const float scaledry = 2;
const float scaledamp = 0.4f;
const float scaleroom = 0.28f;
const float offsetroom = 0.7f;
const float initialroom = 0.5f;
const float initialdamp = 0.5f;
const float initialwet = 1 / scalewet;
const float initialdry = 0;
const float initialwidth = 1;
const float initialmode = 0;
const float freezemode = 0.5f;
const int32_t stereospread = 23;

// These values assume 44.1KHz sample rate
// they will probably be OK for 48KHz sample rate
// but would need scaling for 96KHz (or other) sample rates.
// The values were obtained by listening tests.
const int32_t combtuningL1 = 1116;
const int32_t combtuningR1 = 1116 + stereospread;
const int32_t combtuningL2 = 1188;
const int32_t combtuningR2 = 1188 + stereospread;
const int32_t combtuningL3 = 1277;
const int32_t combtuningR3 = 1277 + stereospread;
const int32_t combtuningL4 = 1356;
const int32_t combtuningR4 = 1356 + stereospread;
const int32_t combtuningL5 = 1422;
const int32_t combtuningR5 = 1422 + stereospread;
const int32_t combtuningL6 = 1491;
const int32_t combtuningR6 = 1491 + stereospread;
const int32_t combtuningL7 = 1557;
const int32_t combtuningR7 = 1557 + stereospread;
const int32_t combtuningL8 = 1617;
const int32_t combtuningR8 = 1617 + stereospread;
const int32_t allpasstuningL1 = 556;
const int32_t allpasstuningR1 = 556 + stereospread;
const int32_t allpasstuningL2 = 441;
const int32_t allpasstuningR2 = 441 + stereospread;
const int32_t allpasstuningL3 = 341;
const int32_t allpasstuningR3 = 341 + stereospread;
const int32_t allpasstuningL4 = 225;
const int32_t allpasstuningR4 = 225 + stereospread;

// ends
