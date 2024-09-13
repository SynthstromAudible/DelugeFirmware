#include "pack.h"
#include <string.h>

// This is the same packing format as used by Sequential synthesizers
// See the "Packed Data Format" section of any DSI or sequential manual.

int32_t pack_8bit_to_7bit(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len) {
	int32_t packets = (src_len + 6) / 7;
	int32_t missing = (7 * packets - src_len); // allow incomplete packets
	int32_t out_len = 8 * packets - missing;
	if (out_len > dst_size)
		return 0;

	for (int32_t i = 0; i < packets; i++) {
		int32_t ipos = 7 * i;
		int32_t opos = 8 * i;
		dst[opos] = 0;
		for (int32_t j = 0; j < 7; j++) {
			// incomplete packet
			if (!(ipos + j < src_len))
				break;
			dst[opos + 1 + j] = src[ipos + j] & 0x7f;
			if (src[ipos + j] & 0x80) {
				dst[opos] |= (1 << j);
			}
		}
	}
	return out_len;
}

int32_t unpack_7bit_to_8bit(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len) {
	int32_t packets = (src_len + 7) / 8;
	int32_t missing = (8 * packets - src_len);
	if (missing == 7) { // this would be weird
		packets--;
		missing = 0;
	}
	int32_t out_len = 7 * packets - missing;
	if (out_len > dst_size)
		return 0;
	for (int32_t i = 0; i < packets; i++) {
		int32_t ipos = 8 * i;
		int32_t opos = 7 * i;
		for (int32_t j = 0; j < 7; j++) {
			if (!(j + 1 + ipos < src_len))
				break;
			dst[opos + j] = src[ipos + 1 + j] & 0x7f;
			if (src[ipos] & (1 << j)) {
				dst[opos + j] |= 0x80;
			}
		}
	}
	return 8 * packets - missing;
}

const int32_t MAX_DENSE_SIZE = 5;
const int32_t MAX_REP_SIZE = (31 + 127);
static int32_t pack_dense(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len) {
	if (src_len > dst_size - 1)
		return -1;
	int32_t highbits = 0;
	for (int32_t j = 0; j < src_len; j++) {
		dst[j + 1] = src[j] & 0x7f;
		if (src[j] & 0x80) {
			highbits |= (1 << j);
		}
	}
	int32_t off = 0;
	switch (src_len) {
	case 2:
		off = 0;
		break;
	case 3:
		off = 4;
		break;
	case 4:
		off = 12;
		break;
	case 5:
		off = 28;
		break;
	default:
		return -1;
	}
	dst[0] = off + highbits;
	return src_len + 1;
}

int32_t pack_8to7_rle(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len) {
	int32_t d = 0;
	int32_t s = 0;

	int32_t i = 0; // start of outer (dense) run
	while (s < src_len) {
		// no matter what happens, we are gonna need at least 2 more bytes..
		if (d > dst_size - 2)
			return -1;
		int32_t k = s; // start of inner (repeated) run
		uint8_t val = src[s++];
		while (s < src_len && (s - k) < MAX_REP_SIZE) {
			if (src[s] != val)
				break;
			s++;
		}
		int32_t dense_size = k - i;
		int32_t rep_size = s - k;
		if (rep_size < 2) {
			dense_size += rep_size;
			rep_size = 0;
			if (dense_size < MAX_DENSE_SIZE && s < src_len) {
				// if there is more data, reconsider after next run
				continue;
			}
		}

		if (dense_size > 0) {
			if (dense_size == 1) {
				dst[d++] = 64 + (1 << 1) + ((src[i] & 0x80) ? 1 : 0);
				dst[d++] = src[i] & 0x7f;
			}
			else {
				int32_t siz = pack_dense(dst + d, dst_size - d, src + i, dense_size);
				if (siz < 0)
					return siz;
				d += siz;
			}
		}

		if (rep_size > 0) {
			if (d > dst_size - 2 - (rep_size >= 31))
				return -2;
			int32_t first = 64 + ((val & 0x80) ? 1 : 0);
			if (rep_size < 31) {
				dst[d++] = first + (rep_size << 1);
			}
			else {
				dst[d++] = first + (31 << 1);
				dst[d++] = (rep_size - 31);
			}
			dst[d++] = val & 0x7f;
		}

		i = s;
	}
	return d;
}

int32_t unpack_7to8_rle(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len) {
	int32_t d = 0;
	int32_t s = 0;

	while (s + 1 < src_len) {
		uint8_t first = src[s++];
		if (first < 64) {
			int32_t size = 0, off = 0;
			if (first < 4) {
				size = 2;
				off = 0;
			}
			else if (first < 12) {
				size = 3;
				off = 4;
			}
			else if (first < 28) {
				size = 4;
				off = 12;
			}
			else if (first < 60) {
				size = 5;
				off = 28;
			}
			else {
				return -7;
			}

			if (size > src_len - s) {
				return -1;
			}
			if (size > dst_size - d)
				return -11;
			int32_t highbits = first - off;
			for (int32_t j = 0; j < size; j++) {
				dst[d + j] = src[s + j] & 0x7f;
				if (highbits & (1 << j)) {
					dst[d + j] |= 0x80;
				}
			}

			d += size;
			s += size;
		}
		else {
			// first = 64 + (runlen<<1) + highbit
			first = first - 64;
			int32_t high = (first & 1);
			int32_t runlen = first >> 1;
			if (runlen == 31) {
				runlen = 31 + src[s++];
				if (s == src_len)
					return -3;
			}
			int32_t byte = src[s++] + 128 * high;
			if (runlen > dst_size - d)
				return -12;
			memset(dst + d, byte, runlen);
			d += runlen;
		}
	}
	return d;
}

// derived from png reference implementation (MIT license)

// Table of CRCs of all 8-bit messages.
static uint32_t crc_table[256];

/* Make the table for a fast CRC. */
void init_crc_table(void) {
	uint32_t c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (uint32_t)n;
		for (k = 0; k < 8; k++) {
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[n] = c;
	}
}

// Update a running CRC with the bytes buf[0..len-1]--the CRC
// should be initialized to all 1's, and the transmitted value
// is the 1's complement of the final running CRC (see the
// crc() routine below).

static uint32_t update_crc(uint32_t crc, uint8_t* buf, int len) {
	uint32_t c = crc;
	int n;

	for (n = 0; n < len; n++) {
		c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

// Return the CRC of the bytes buf[0..len-1].
uint32_t get_crc(uint8_t* buf, int len) {
	return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}
