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

/// RZ/A1L implementation of <libdeluge/control_surface.h>.
///
/// Part of bsp-rza1-legacy. The control surface is the Deluge's PIC co-processor
/// (pads/buttons/encoders/LEDs over UART); this file forwards the boundary's
/// logical operations to the PIC protocol class, which stays BSP-internal.
///
/// This file (and the PIC class) is now app-header-free — colours cross as raw
/// r,g,b bytes, dimensions come from the HAL-free board_layout.hpp — so it is
/// compiled into the bsp_rza1 library proper. It still references legacy-driver
/// symbols (uart transport, etc.) that are compiled into the `deluge` target;
/// those are resolved at the final link, the same as midi_io.c / cv_gate.c, until
/// the drivers themselves are decoupled and relocated into the library (Phase C).

#include "libdeluge/control_surface.h"
#include "libdeluge/display.h" // deluge_display_write_seven_segment (PIC-driven 7-seg)

#include "RZA1/cpu_specific.h"
#include "board_layout.hpp" // kNumGoldKnobIndicatorLEDs, kNumericDisplayLength (HAL-free)
#include "drivers/pic/pic.h"
#include <array>
#include <cstddef>

void deluge_control_read_boot_info(DelugeBootInfo* out) {
	out->pic_firmware_version = 0;
	out->oled_present = false;
	out->factory_reset_requested = false;

	PIC::requestFirmwareVersion(); // Request PIC firmware version
	PIC::resendButtonStates();     // Tell PIC to re-send button states
	PIC::flush();

	bool readingFirmwareVersion = false;
	bool otherButtonsOrEvents = false;

	PIC::read(0x8000, [&](PIC::Response response) -> int32_t {
		if (readingFirmwareVersion) {
			readingFirmwareVersion = false;
			uint8_t value = static_cast<uint8_t>(response);
			out->pic_firmware_version = value & 127;
			out->oled_present = (value & 128) != 0;
			return 0;
		}

		switch (response) {
		case PIC::Response::FIRMWARE_VERSION_NEXT:
			readingFirmwareVersion = true;
			return 0;

		case PIC::Response::RESET_SETTINGS:
			// Latch the request; the application performs the reset once the read
			// completes. Only honour it if no other button/event was seen first.
			if (!otherButtonsOrEvents) {
				out->factory_reset_requested = true;
			}
			return 0;

		case PIC::Response::UNKNOWN_BREAK:
			return 1;

		case PIC::Response::UNKNOWN_BOOT_RESPONSE: // value 129. Happens every boot.
			return 0;

		default:
			if (response >= PIC::Response::UNKNOWN_OLED_RELATED_COMMAND && response <= PIC::Response::SET_DC_HIGH) {
				// OLED D/C low ack
				return 0;
			}
			// If any hint of another button being held, don't do anything.
			otherButtonsOrEvents = true;
			return 0;
		}
	});
}

void deluge_control_set_led(uint8_t led, bool on) {
	if (on) {
		PIC::setLEDOn(led);
	}
	else {
		PIC::setLEDOff(led);
	}
}

void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count) {
	std::array<uint8_t, kNumGoldKnobIndicatorLEDs> ring{};
	for (size_t i = 0; i < kNumGoldKnobIndicatorLEDs && i < count; i++) {
		ring[i] = levels[i];
	}
	PIC::setGoldKnobIndicator(which != 0, ring);
}

uint32_t deluge_control_pad_output_space(void) {
	// uartGetTxBufferSpace / UART_ITEM_PIC_PADS come via the PIC transport header.
	return (uint32_t)uartGetTxBufferSpace(UART_ITEM_PIC_PADS);
}

void deluge_control_flush(void) {
	PIC::flush();
}

bool deluge_control_poll_resume(void) {
	return PIC::read() == PIC::Response::RESET_SETTINGS;
}

// The Deluge's 7-segment display is driven over the PIC, so its blit lives with
// the PIC adapter even though it is declared in <libdeluge/display.h>.
DelugeStatus deluge_display_write_seven_segment(const uint8_t* digits, uint8_t count) {
	(void)count; // == kNumericDisplayLength
	PIC::update7SEG(*reinterpret_cast<const std::array<uint8_t, kNumericDisplayLength>*>(digits));
	return DELUGE_OK;
}

// DelugeColour is {uint8_t r, g, b}, so a colour array is already the r,g,b byte
// stream the PIC wants — pass it by reinterpret, no per-pad copy on these hot paths.
void deluge_control_set_pad_columns(uint8_t idx, const DelugeColour* colours, uint8_t count) {
	PIC::setColourForTwoColumns(idx, reinterpret_cast<const uint8_t*>(colours), count);
}

void deluge_control_flash_pad(uint8_t idx) {
	PIC::flashMainPad(idx);
}

void deluge_control_flash_pad_colour(uint8_t idx, int32_t colour_idx) {
	PIC::flashMainPadWithColourIdx(idx, colour_idx);
}

void deluge_control_scroll_horizontal(uint8_t flags) {
	PIC::setupHorizontalScroll(flags);
}

void deluge_control_scroll_vertical(bool up, const DelugeColour* colours, uint8_t count) {
	PIC::doVerticalScroll(up, reinterpret_cast<const uint8_t*>(colours), count);
}

void deluge_control_scroll_row(uint8_t row, DelugeColour colour) {
	PIC::sendScrollRow(row, colour.r, colour.g, colour.b);
}

void deluge_control_scroll_done(void) {
	PIC::doneSendingRows();
}

void deluge_control_set_refresh_time(uint8_t ms) {
	PIC::setRefreshTime(ms);
}

void deluge_control_set_dimmer_interval(uint8_t interval) {
	PIC::setDimmerInterval(interval);
}
