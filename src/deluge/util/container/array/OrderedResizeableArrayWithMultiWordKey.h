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

#ifndef ORDEREDRESIZEABLEARRAYWITHMULTIWORDKEY_H_
#define ORDEREDRESIZEABLEARRAYWITHMULTIWORDKEY_H_

#include "OrderedResizeableArray.h"

// This class extends OrderedResizeableArrayWith32bitKey so that if we wish, we can still use that class's single-word search functions, where we only care about the first word.

class OrderedResizeableArrayWithMultiWordKey : public OrderedResizeableArrayWith32bitKey {
public:
	OrderedResizeableArrayWithMultiWordKey(int newElementSize = sizeof(uint32_t) * 2, int newNumWordsInKey = 2);
	int searchMultiWord(uint32_t* __restrict__ keyWords, int comparison, int rangeBegin = 0, int rangeEnd = -1);
	int searchMultiWordExact(uint32_t* __restrict__ keyWords, int* getIndexToInsertAt = NULL, int rangeBegin = 0);
	int insertAtKeyMultiWord(uint32_t* __restrict__ keyWords, int rangeBegin = 0, int rangeEnd = -1);
	bool deleteAtKeyMultiWord(uint32_t* __restrict__ keyWords);

	int numWordsInKey;
};

#endif /* ORDEREDRESIZEABLEARRAYWITHMULTIWORDKEY_H_ */
