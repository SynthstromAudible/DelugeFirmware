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

#include "util/name_compare.h"
#include <cstring>

// Returns 100000 if the string is not a note name.
// The returned number is *not* a MIDI note. It's arbitrary, used for comparisons only.
// noteChar has been made lowercase, which is why we can't just take it from the string.
ComparativeNoteNumber getComparativeNoteNumberFromChars(char const* string, char noteChar, bool octaveStartsFromA) {
	char const* stringStart = string;
	ComparativeNoteNumber toReturn{};

	toReturn.noteNumber = noteChar - 'a';

	if (!octaveStartsFromA) {
		toReturn.noteNumber -= 2;
		if (toReturn.noteNumber < 0) {
			toReturn.noteNumber += 7;
		}
	}

	toReturn.noteNumber *= 3; // To make room for flats and sharps, below.

	string++;
	if (*string == 'b') {
		toReturn.noteNumber--;
		string++;
	}
	else if (*string == '#') {
		toReturn.noteNumber++;
		string++;
	}

	bool numberIsNegative = false;
	if (*string == '-') {
		numberIsNegative = true;
		string++;
	}

	if (*string < '1' || *string > '9') { // There has to be at least some number there if we're to consider this a note
		                                  // name. And it can't start with 0.
		toReturn.noteNumber = 100000;
		toReturn.stringLength = 0;
		return toReturn;
	}

	int32_t number = *string - '0';
	string++;

	while (true) {

		if (*string >= '0' && *string <= '9') {
			number *= 10;
			number += *string - '0';
			string++;
		}
		else {
			if (numberIsNegative) {
				number = -number;
			}
			toReturn.noteNumber += number * 36;
			toReturn.stringLength = string - stringStart;
			return toReturn;
		}
	}
}

// You must set this at some point before calling strcmpspecial. This isn't implemented as an argument because
// sometimes you want to set it way up the call tree, and passing it all the way down is a pain.
bool shouldInterpretNoteNames;

bool octaveStartsFromA; // You must set this if setting shouldInterpretNoteNames to true.

// Returns positive if first > second
// Returns negative if first < second
int32_t strcmpspecial(char const* first, char const* second) {

	int32_t resultIfGetToEndOfBothStrings = 0;

	while (true) {
		bool firstIsFinished = (*first == 0);
		bool secondIsFinished = (*second == 0);

		if (firstIsFinished && secondIsFinished) {
			return resultIfGetToEndOfBothStrings; // If both are finished
		}

		if (firstIsFinished || secondIsFinished) { // If just one is finished
			return (int32_t)*first - (int32_t)*second;
		}

		bool firstIsNumber = (*first >= '0' && *first <= '9');
		bool secondIsNumber = (*second >= '0' && *second <= '9');

		// If they're both numbers...
		if (firstIsNumber && secondIsNumber) {

			// If we haven't yet seen a differing number of leading zeros in a number, see if that exists here.
			if (!resultIfGetToEndOfBothStrings) {
				char const* firstHere = first;
				char const* secondHere = second;
				while (true) {
					char firstChar = *firstHere;
					char secondChar = *secondHere;
					bool firstDigitIsLeadingZero = (firstChar == '0');
					bool secondDigitIsLeadingZero = (secondChar == '0');

					if (firstDigitIsLeadingZero && secondDigitIsLeadingZero) { // If both are zeros, look at next chars.
						firstHere++;
						secondHere++;
						continue;
					}

					// else if (!firstDigitIsLeadingZero || !secondDigitIsLeadingZero) break;	// If both are not
					// zeros, we're done.
					//  Actually, the same end result is achieved without that line.

					// If we're still here, one is a leading zero and the other isn't.
					resultIfGetToEndOfBothStrings =
					    (int32_t)firstChar
					    - (int32_t)secondChar; // Will still end up as zero when that needs to happen.
					break;
				}
			}

			int32_t firstNumber = *first - '0';
			int32_t secondNumber = *second - '0';
			first++;
			second++;

			while (*first >= '0' && *first <= '9') {
				firstNumber *= 10;
				firstNumber += *first - '0';
				first++;
			}

			while (*second >= '0' && *second <= '9') {
				secondNumber *= 10;
				secondNumber += *second - '0';
				second++;
			}

			int32_t difference = firstNumber - secondNumber;
			if (difference) {
				return difference;
			}
		}

		// Otherwise, if not both numbers...
		else {

			char firstChar = *first;
			char secondChar = *second;

			// Make lowercase
			if (firstChar >= 'A' && firstChar <= 'Z') {
				firstChar += 32;
			}
			if (secondChar >= 'A' && secondChar <= 'Z') {
				secondChar += 32;
			}

			// If we're doing note ordering...
			if (shouldInterpretNoteNames) {
				ComparativeNoteNumber firstResult{}, secondResult{};
				firstResult.noteNumber = 100000;
				firstResult.stringLength = 0;
				secondResult.noteNumber = 100000;
				secondResult.stringLength = 0;

				if (firstChar >= 'a' && firstChar <= 'g') {
					firstResult = getComparativeNoteNumberFromChars(first, firstChar, octaveStartsFromA);
				}

				if (secondChar >= 'a' && secondChar <= 'g') {
					secondResult = getComparativeNoteNumberFromChars(second, secondChar, octaveStartsFromA);
				}

				if (firstResult.noteNumber == secondResult.noteNumber) {
					if (!firstResult.stringLength && !secondResult.stringLength) {
						goto doNormal;
					}
					first += firstResult.stringLength;
					second += secondResult.stringLength;
				}
				else {
					return firstResult.noteNumber - secondResult.noteNumber;
				}
			}

			else {
doNormal:
				// If they're the same, carry on
				if (firstChar == secondChar) {
					first++;
					second++;
				}

				// Otherwise...
				else {
					// Dot then underscore comes first
					if (firstChar == '.') {
						return -1;
					}
					else if (secondChar == '.') {
						return 1; // We know they're not both the same - see above.
					}

					if (firstChar == '_') {
						return -1;
					}
					else if (secondChar == '_') {
						return 1;
					}

					return (int32_t)firstChar - (int32_t)secondChar;
				}
			}
		}
	}
}
