/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "deluge-project/midi_command_codec.h"

#include "serialization/tree.h"

#include <cstdio>
#include <string>

namespace deluge::project {

using serialization::text_of;
using serialization::Value;

namespace {

using Port = MidiDeviceRef::Port;

/// The text a hex attribute holds: `0x` and @p digits upper-case digits.
Value hex_text(uint32_t value, int digits) {
	char text[11];
	std::snprintf(text, sizeof text, "0x%0*X", digits, value);
	return Value::makeScalar(text);
}

} // namespace

Value encode_device(const MidiDeviceRef& device) {
	auto map = Value::makeMap();
	switch (device.port) {
	case Port::DIN:
		map.set("port", Value::makeScalar("din"));
		break;
	case Port::UPSTREAM_USB_1:
		map.set("port", Value::makeScalar("upstreamUSB"));
		break;
	case Port::UPSTREAM_USB_2:
		map.set("port", Value::makeScalar("upstreamUSB2"));
		break;
	case Port::UPSTREAM_USB_3:
		map.set("port", Value::makeScalar("upstreamUSB3"));
		break;
	case Port::HOSTED:
		map.set("name", Value::makeScalar(device.name));
		map.set("vendorId", hex_text(device.vendor_id, 4));
		map.set("productId", hex_text(device.product_id, 4));
		break;
	}
	return map;
}

std::optional<MidiDeviceRef> decode_device(const Value& node) {
	if (!node.isMap()) {
		return std::nullopt;
	}
	if (const std::string* port = text_of(node.find("port"))) {
		if (*port == "upstreamUSB") {
			return MidiDeviceRef{.port = Port::UPSTREAM_USB_1};
		}
		if (*port == "upstreamUSB2") {
			return MidiDeviceRef{.port = Port::UPSTREAM_USB_2};
		}
		if (*port == "upstreamUSB3") {
			return MidiDeviceRef{.port = Port::UPSTREAM_USB_3};
		}
		if (*port == "din") {
			return MidiDeviceRef{.port = Port::DIN};
		}
	}
	MidiDeviceRef hosted{.port = Port::HOSTED};
	if (const std::string* name = text_of(node.find("name"))) {
		hosted.name = *name;
	}
	hosted.vendor_id = static_cast<uint16_t>(serialization::hex_of(node.find("vendorId")).value_or(0));
	hosted.product_id = static_cast<uint16_t>(serialization::hex_of(node.find("productId")).value_or(0));
	if (!hosted.name.empty() || hosted.vendor_id != 0) {
		return hosted;
	}
	return std::nullopt;
}

} // namespace deluge::project
