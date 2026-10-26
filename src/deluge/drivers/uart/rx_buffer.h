#ifndef DELUGE_DRIVERS_UART_RX_BUFFER_H
#define DELUGE_DRIVERS_UART_RX_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

// Inspect a snapshot of the unread part of a power-of-two DMA ring without
// advancing its consumer. Replacing all matches also consumes duplicate acks.
// Call only from the same task context as the normal consumer, not an ISR.
static inline bool uartReplaceByteInRing(volatile uint8_t* buffer, uint32_t size, uint32_t read_pos, uint32_t write_pos,
                                         uint8_t value, uint8_t replacement) {
	bool found = false;
	uint32_t mask = size - 1;
	write_pos &= mask;
	for (uint32_t pos = read_pos & mask; pos != write_pos; pos = (pos + 1) & mask) {
		if (buffer[pos] == value) {
			buffer[pos] = replacement;
			found = true;
		}
	}
	return found;
}

#endif // DELUGE_DRIVERS_UART_RX_BUFFER_H
