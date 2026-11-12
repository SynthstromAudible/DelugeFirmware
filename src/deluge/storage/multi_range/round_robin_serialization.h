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

/// Reads a zone's "rrMode", returning Cycle (0) for a value this firmware has no mode for.
///
/// Read into a full int before validating, so a hand-edited 256 can't wrap to a legal-looking 0. An unknown
/// value isn't only a corrupt file: a preset saved by newer firmware can carry a mode this one predates.
/// Playback already treats anything unknown as Cycle, but the MODE menu indexes its option list with the
/// stored value, so it has to be a real one by the time it's stored.
///
/// Templated on the deserializer for host tests - see tests/unit/round_robin_parser_tests.cpp.
template <typename DeserializerT>
uint8_t readRRModeFromFile(DeserializerT& reader, char const* tagName) {
	int32_t value = reader.readTagOrAttributeValueInt();
	reader.exitTag(tagName);
	if (value < 0 || value >= MultisampleRange::kNumRRModes) {
		return 0;
	}
	return (uint8_t)value;
}

/// Reads a slot's "variantVolume", returning unity for anything outside 0-50.
///
/// Read into a full int before validating: narrowed straight to uint8_t, a hand-edited 300 would become a
/// believable 44. A value above unity can't boost - the render path only ever attenuates - but the VOL item
/// would show it, so it gets the same treatment as a value this firmware can't act on at all.
template <typename DeserializerT>
uint8_t readVariantVolumeFromFile(DeserializerT& reader, char const* tagName) {
	int32_t value = reader.readTagOrAttributeValueInt();
	reader.exitTag(tagName);
	if (value < 0 || value > kVariantVolumeUnity) {
		return kVariantVolumeUnity;
	}
	return (uint8_t)value;
}

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
				alternateHolder->volume = kVariantVolumeUnity;
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
				else if (!strcmp(tagName, "variantVolume")) {
					alternateHolder->volume = readVariantVolumeFromFile(reader, tagName);
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
			reader.match('}');          // exit the "alternate" value object
			reader.exitTag(NULL, true); // exit the array item, as Sound's sampleRanges reader does

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
