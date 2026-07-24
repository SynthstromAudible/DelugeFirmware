/*
 * Copyright (c) 2024-2026 Synthstrom Audible Limited
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

#include "storage/multi_range/multisample_range.h"
#include <cstring>

/// Reads a "roundRobinAlternates" array into the given range. Deliberately does NOT touch rrMode -
/// that's stored in its own sibling tag which may appear before this one in the file.
///
/// Templated on the deserializer and range types so the parsing logic can be unit-tested on the
/// host with lightweight fakes (see tests/unit/round_robin_parser_tests.cpp). Firmware code
/// instantiates it with Deserializer and MultisampleRange.
template <typename DeserializerT, typename RangeT>
Error readRoundRobinAlternates(DeserializerT& reader, RangeT* range) {
	char const* tagName;

	range->rrCount = 0;
	range->rrIndex = 0;

	reader.match('[');
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "alternate")) {
			decltype(range->ensureAlternateHolder(0)) alternateHolder = nullptr;
			if (range->rrCount < kMaxRoundRobinAlternates) {
				alternateHolder = range->ensureAlternateHolder(range->rrCount);
				if (!alternateHolder) {
					return Error::INSUFFICIENT_RAM;
				}
				alternateHolder->startPos = 0;
				alternateHolder->endPos = 0;
				alternateHolder->loopStartPos = 0;
				alternateHolder->loopEndPos = 0;
				alternateHolder->transpose = 0;
				alternateHolder->setCents(0);
			}

			reader.match('{');
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (alternateHolder == nullptr) {
					// Past the slot cap - skip the whole field like any unknown tag.
					reader.exitTag(tagName);
				}
				else if (!strcmp(tagName, "fileName")) {
					reader.readTagOrAttributeValueString(&alternateHolder->filePath);
					reader.exitTag("fileName");
				}
				else if (!strcmp(tagName, "transpose")) {
					alternateHolder->transpose = reader.readTagOrAttributeValueInt();
					reader.exitTag("transpose");
				}
				else if (!strcmp(tagName, "cents")) {
					alternateHolder->setCents(reader.readTagOrAttributeValueInt());
					reader.exitTag("cents");
				}
				else if (!strcmp(tagName, "zone")) {
					reader.match('{');
					while (*(tagName = reader.readNextTagOrAttributeName())) {
						if (!strcmp(tagName, "startSamplePos")) {
							alternateHolder->startPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("startSamplePos");
						}
						else if (!strcmp(tagName, "endSamplePos")) {
							alternateHolder->endPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("endSamplePos");
						}
						else if (!strcmp(tagName, "startLoopPos")) {
							alternateHolder->loopStartPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("startLoopPos");
						}
						else if (!strcmp(tagName, "endLoopPos")) {
							alternateHolder->loopEndPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("endLoopPos");
						}
						else {
							reader.exitTag(tagName);
						}
					}
					reader.exitTag("zone", true);
				}
				else {
					reader.exitTag(tagName);
				}
			}
			reader.exitTag("alternate", true);

			if (alternateHolder != nullptr) {
				range->rrCount++;
			}
		}
		else {
			reader.exitTag();
		}
	}

	reader.exitTag();
	reader.match(']');
	return Error::NONE;
}
