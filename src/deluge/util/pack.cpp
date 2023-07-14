#include "pack.h"
#include <cstring>

// This is the same packing format as used by Sequential synthesizers
// See the "Packed Data Format" section of any DSI or sequential manual.

int pack_8bit_to_7bit(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	int packets = (src_len + 6) / 7;
	int missing = (7 * packets - src_len); // allow incomplete packets
	int out_len = 8 * packets - missing;
	if (out_len > dst_size)
		return 0;

	for (int i = 0; i < packets; i++) {
		int ipos = 7 * i;
		int opos = 8 * i;
		memset(dst + opos, 0, 8);
		for (int j = 0; j < 7; j++) {
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

int unpack_7bit_to_8bit(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	int packets = (src_len + 7) / 8;
	int missing = (8 * packets - src_len);
	if (missing == 7) { // this would be weird
		packets--;
		missing = 0;
	}
	int out_len = 7 * packets - missing;
	if (out_len > dst_size)
		return 0;
	for (int i = 0; i < packets; i++) {
		int ipos = 8 * i;
		int opos = 7 * i;
		memset(dst + opos, 0, 7);
		for (int j = 0; j < 7; j++) {
			if (!(j + 1 + ipos < src_len))
				break;
			dst[opos + j] = src[ipos + 1 + j] & 0x7f;
			if (src[ipos] & (1 << j)) {
				dst[opos + j] |= 0x80;
			}
		}
	}
	return 8 * packets;
}

const int MAX_DENSE_SIZE = 5;
const int MAX_REP_SIZE = (31 + 127);
static int pack_dense(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	if (src_len > dst_size - 1)
		return -1;
	int highbits = 0;
	for (int j = 0; j < src_len; j++) {
		dst[j + 1] = src[j] & 0x7f;
		if (src[j] & 0x80) {
			highbits |= (1 << j);
		}
	}
	int off = 0;
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

int pack_8to7_rle(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	int d = 0;
	int s = 0;

	int i = 0; // start of outer (dense) run
	while (s < src_len) {
		// no matter what happens, we are gonna need at least 2 more bytes..
		if (d > dst_size - 2)
			return -1;
		int k = s; // start of inner (repeated) run
		uint8_t val = src[s++];
		while (s < src_len && (s - k) < MAX_REP_SIZE) {
			if (src[s] != val)
				break;
			s++;
		}
		int dense_size = k - i;
		int rep_size = s - k;
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
				int siz = pack_dense(dst + d, dst_size - d, src + i, dense_size);
				if (siz < 0)
					return siz;
				d += siz;
			}
		}

		if (rep_size > 0) {
			if (d > dst_size - 2 - (rep_size >= 31))
				return -2;
			int first = 64 + ((val & 0x80) ? 1 : 0);
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

int unpack_7to8_rle(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	int d = 0;
	int s = 0;

	while (s + 1 < src_len) {
		uint8_t first = src[s++];
		if (first < 64) {
			int size = 0, off = 0;
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
			int highbits = first - off;
			for (int j = 0; j < size; j++) {
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
			int high = (first & 1);
			int runlen = first >> 1;
			if (runlen == 31) {
				runlen = 31 + src[s++];
				if (s == src_len)
					return -3;
			}
			int byte = src[s++] + 128 * high;
			if (runlen > dst_size - d)
				return -12;
			memset(dst + d, byte, runlen);
			d += runlen;
		}
	}
	return d;
}
