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

/// RZ/A1L implementation of <libdeluge/cv_gate.h>.
///
/// Part of bsp-rza1-legacy; depends only on the HAL/drivers, so it lives in the
/// clean bsp_rza1 library. Owns the CV DAC command format, the SPI routing (the
/// CV DAC shares RSPI0 with the OLED, so writes are serialised through the OLED
/// transfer queue when a display is present), and the board's gate pin map.

#include "libdeluge/cv_gate.h"

#include "libdeluge/clock.h" // deluge_clock_delay_ms

#include "RZA1/cpu_specific.h"        // SPI_CHANNEL_CV, NUM_GATE_CHANNELS (via board_config)
#include "RZA1/gpio/gpio.h"           // setPinAsOutput, setOutputState
#include "RZA1/oled/oled_low_level.h" // enqueueCVMessage
#include "drivers/rspi/rspi.h"        // R_RSPI_SendBasic32

/// RZ/A1L GPIO {port, pin} of each gate output.
static const uint8_t gate_port[] = {2, 2, 2, 4};
static const uint8_t gate_pin[] = {7, 8, 9, 0};

/// True when the OLED shares RSPI0 with the CV DAC, so CV writes must go through
/// the OLED transfer queue rather than driving the bus directly.
static bool cv_spi_shared = false;

static void cv_send_word(int enqueue_channel, uint32_t word) {
	if (cv_spi_shared) {
		enqueueCVMessage(enqueue_channel, word);
	}
	else {
		R_RSPI_SendBasic32(SPI_CHANNEL_CV, word);
	}
}

void deluge_cv_init(bool display_shares_spi) {
	cv_spi_shared = display_shares_spi;

	// AD DAC linearity routine (LIN=1 then LIN=0), as the original CVEngine::init did.
	cv_send_word(SPI_CHANNEL_CV, 0b00000101000000100000000000000000); // LIN = 1
	deluge_clock_delay_ms(10);
	cv_send_word(SPI_CHANNEL_CV, 0b00000101000000000000000000000000); // LIN = 0
}

void deluge_cv_set(uint8_t channel, uint16_t value) {
	uint32_t word = (uint32_t)(0b00110000 | (1 << channel)) << 24;
	word |= (uint32_t)value << 8;
	cv_send_word(channel, word);
}

void deluge_gate_init(void) {
	for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {
		setPinAsOutput(gate_port[i], gate_pin[i]);
	}
}

void deluge_gate_set(uint8_t channel, bool on) {
	setOutputState(gate_port[channel], gate_pin[channel], on);
}
