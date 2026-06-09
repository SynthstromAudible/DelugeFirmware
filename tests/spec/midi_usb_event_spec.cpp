/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "midi/usb_event.h"
#include <cppspec.hpp>

using namespace deluge::midi;

// clang-format off
describe midi_usb_event("deluge::midi USB-MIDI event packets", $ {
	context("pack_event", _ {
		it("packs a note-on onto cable 0", _ {
			UsbEventPacket p = pack_event(MIDIMessage::noteOn(0, 60, 100));
			expect(p.header).to_equal(0x09); // cable 0, code index number 9 (note-on)
			expect(p.status).to_equal(0x90); // note-on, channel 0
			expect(p.data1).to_equal(60);
			expect(p.data2).to_equal(100);
		});

		it("encodes the virtual cable in the header's high nibble", _ {
			UsbEventPacket p = pack_event(MIDIMessage::cc(1, 7, 64), 2);
			expect(p.cable()).to_equal(2);
			expect(p.codeIndexNumber()).to_equal(0x0B); // control change
			expect(p.status).to_equal(0xB1);            // CC, channel 1
		});

		it("uses code index number 0x3 for a song-position-pointer", _ {
			UsbEventPacket p = pack_event(MIDIMessage::systemPositionPointer(1000));
			expect(p.status).to_equal(0xF2);
			expect(p.codeIndexNumber()).to_equal(0x3);
		});
	});

	context("UsbEventPacket::word", _ {
		it("is little-endian (byte 0 least significant)", _ {
			UsbEventPacket p{.header = 0x09, .status = 0x90, .data1 = 60, .data2 = 100};
			expect(p.word()).to_equal(0x643C9009u);
		});

		it("round-trips through a word", _ {
			UsbEventPacket p = pack_event(MIDIMessage::noteOn(3, 60, 100), 1);
			expect(UsbEventPacket::fromWord(p.word())).to_equal(p);
		});
	});

	context("decode_event", _ {
		it("decodes a note-on, recovering the message and cable", _ {
			std::optional decoded = decode_event(UsbEventPacket{.header = 0x19, .status = 0x95, .data1 = 60, .data2 = 100});
			expect(decoded).to_have_value();
			expect(decoded->cable).to_equal(1);
			expect(decoded->message).to_equal(MIDIMessage::noteOn(5, 60, 100));
		});

		it("round-trips a packed message", _ {
			MIDIMessage msg = MIDIMessage::cc(9, 74, 32);
			expect(decode_event(pack_event(msg, 0))->message).to_equal(msg);
		});

		it("reports 2/3-byte system-common messages as system status 0xF", _ {
			std::optional decoded = decode_event(UsbEventPacket{.header = 0x03, .status = 0xF2, .data1 = 0x10, .data2 = 0x20});
			expect(decoded).to_have_value();
			expect(decoded->message.statusType).to_equal(0x0F);
		});

		it("rejects SysEx fragments (low code index numbers)", _ {
			expect(decode_event(UsbEventPacket{.header = 0x04, .status = 0xF0, .data1 = 0x7E, .data2 = 0x00}).has_value())
			    .to_equal(false);
		});

		it("rejects packets whose data bytes have the high bit set", _ {
			expect(decode_event(UsbEventPacket{.header = 0x09, .status = 0x90, .data1 = 0x80, .data2 = 0x00}).has_value())
			    .to_equal(false);
		});
	});
});

CPPSPEC_SPEC(midi_usb_event);
