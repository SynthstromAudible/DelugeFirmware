/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "fatfs/fatfs.hpp"
#include "model/tuning/tuning.h"
#include "storage/cluster/cluster.h"

#ifndef IN_UNIT_TESTS

#include "storage/storage_manager.h"

#else

#include "storage/ScalaReader.h"

#endif

#include "util/functions.h"
#include "util/try.h"
#include <string.h>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"
}

/*******************************************************************************

    ScalaReader

********************************************************************************/

ScalaReader::ScalaReader() {

	reset();
}

void ScalaReader::reset() {
	resetReader();
}

void ScalaReader::truncateNumber(char allow) {
	char* end = readStart;
	while (*end != '\0') {
		if (*end >= '0' or *end <= '9' or *end == '-' or *end == allow) {
			end++;
		}
		else {
			break;
		}
	}
	*end = '\0';
}

Error ScalaReader::readDescription() {
	// not used
	return Error::NONE;
}

Error ScalaReader::readDivisions() {
	divisions = stringToInt(readStart);
	if (divisions <= 0)
		return Error::INVALID_SCALA_FORMAT;
	if (divisions > MAX_DIVISIONS)
		return Error::FILE_UNSUPPORTED;

	TuningSystem::tuning->setDivisions(divisions);
	return Error::NONE;
}

Error ScalaReader::readRatio(char* slash) {
	truncateNumber('/');
	int32_t numerator = memToUIntOrError(readStart, slash);
	int32_t denominator = stringToInt(slash + 1);
	if ((numerator <= 0) != (denominator <= 0)) {
		// no negative ratios
		return Error::INVALID_SCALA_FORMAT;
	}
	TuningSystem::tuning->setNextRatio(numerator, denominator);
	return Error::NONE;
}

Error ScalaReader::readCents() {
	truncateNumber('.');
	double cents = stringToDouble(readStart);
	TuningSystem::tuning->setNextCents(cents);
	return Error::NONE;
}

Error ScalaReader::readInteger() {
	truncateNumber('0');
	int32_t integer = stringToInt(readStart);
	if (integer < 0) {
		// no negative ratios
		return Error::INVALID_SCALA_FORMAT;
	}
	TuningSystem::tuning->setNextRatio(integer, 1);
	return Error::NONE;
}

Error ScalaReader::readPitch() {
	char* slash = strchr(readStart, '/');
	if (slash != NULL) {
		readRatio(slash);
	}
	else if (strchr(readStart, '.') != NULL) {
		readCents();
	}
	else {
		Error err = readInteger();
		if (err != Error::NONE) {
			return err;
		}
	}
	return Error::NONE;
}

void ScalaReader::skipWhiteSpace() {
	while (*readStart != '\0') {
		if (*readStart == ' ' || *readStart == '\t') {
			readStart++;
		}
		else {
			return;
		}
	}
}

Error ScalaReader::parseLine() {
	Error err = Error::NONE;

	// skip comments
	if (*readStart == '!')
		return err;

	skipWhiteSpace();

	if (effectiveLine == 0) {
		// first non-comment line is the description (can be blank)
		err = readDescription();
	}
	else if (*readStart == '\0') {
		// skip empty lines
		return err;
	}
	else if (effectiveLine == 1) {
		// second non-commented line is the number of notes
		err = readDivisions();
		if (err != Error::NONE) {
			printf("invalid divisions\n");
		}
	}
	else if (effectiveLine - 1 <= divisions) {
		// all remaining non-commented lines are pitch values
		err = readPitch();
	}

	if (err == Error::NONE) {
		effectiveLine++;
	}

	return err;
}

Error ScalaReader::openScalaFile(FilePointer* filePointer, const char* name) {

	effectiveLine = 0;
	divisions = 0;
	Error err;

	TuningSystem::tuning->setup(name);
	TuningSystem::tuning->setNextCents(0); // first pitch is C# not C
	readStart = fileClusterBuffer;

	while (readLine(readStart)) {
		printf("%s\n", readStart);
		err = parseLine();

		if (err != Error::NONE) {
			printf("%s:%d\n", name, effectiveLine);
			return err;
		}
		readStart = fileClusterBuffer;
	}
	return Error::NONE;
}
