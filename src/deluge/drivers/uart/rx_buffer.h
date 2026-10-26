#ifndef DELUGE_DRIVERS_UART_RX_BUFFER_H
#define DELUGE_DRIVERS_UART_RX_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

// Leave one slot empty so the DMA producer cannot catch the consumer and make
// a full ring indistinguishable from an empty one.
static inline uint32_t uartRingSpace(uint32_t size, uint32_t read_pos, uint32_t write_pos) {
	return (read_pos - write_pos - 1) & (size - 1);
}

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

// Main-loop staging keeps deferred panel input out of the DMA ring. One slot
// stays free for uartInsertFakeChar; normal put-back immediately follows a read.
struct UartRxQueue {
	uint8_t bytes[256];
	uint32_t read_pos;
	uint32_t count;
};

static inline bool uartReadStagedByte(struct UartRxQueue* queue, uint8_t* value) {
	if (!queue->count) {
		return false;
	}
	*value = queue->bytes[queue->read_pos];
	queue->read_pos = (queue->read_pos + 1) % sizeof(queue->bytes);
	--queue->count;
	return true;
}

static inline void uartPrependStagedByte(struct UartRxQueue* queue, uint8_t value) {
	queue->read_pos = (queue->read_pos + sizeof(queue->bytes) - 1) % sizeof(queue->bytes);
	queue->bytes[queue->read_pos] = value;
	++queue->count;
}

static inline bool uartStagePendingBytes(struct UartRxQueue* queue, volatile uint8_t* buffer, uint32_t size,
                                         uint32_t* read_pos, uint32_t write_pos, uint8_t value, uint8_t replacement) {
	bool found = false;
	uint32_t retained = 0;
	// An acknowledgement may already have been staged before its request was
	// committed. Remove it here as well, preserving the order of panel bytes.
	for (uint32_t offset = 0; offset < queue->count; ++offset) {
		uint8_t byte = queue->bytes[(queue->read_pos + offset) % sizeof(queue->bytes)];
		if (byte == value) {
			found = true;
		}
		else if (byte != replacement) {
			queue->bytes[(queue->read_pos + retained++) % sizeof(queue->bytes)] = byte;
		}
	}
	queue->count = retained;
	// Scan even when staging is full: committed SELECT/DESELECT must finish.
	found = uartReplaceByteInRing(buffer, size, *read_pos, write_pos, value, replacement) || found;
	uint32_t mask = size - 1;
	write_pos &= mask;
	while (*read_pos != write_pos) {
		uint8_t byte = buffer[*read_pos];
		if (byte != replacement) {
			if (queue->count >= sizeof(queue->bytes) - 1) {
				break;
			}
			queue->bytes[(queue->read_pos + queue->count++) % sizeof(queue->bytes)] = byte;
		}
		*read_pos = (*read_pos + 1) & mask;
	}
	return found;
}

#endif // DELUGE_DRIVERS_UART_RX_BUFFER_H
