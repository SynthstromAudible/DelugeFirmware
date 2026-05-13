// Fuzz harness for the SysEx 7-bit pack / unpack and RLE routines in
// src/deluge/util/pack.c. These functions sit in front of every SysEx
// parsing path on the device, so any out-of-bounds write or read here
// is reachable by an attacker over USB-MIDI.
//
// Build via tests/fuzz/CMakeLists.txt (libFuzzer requires clang).
// Run with:   ./build/pack_fuzz -max_len=8192

#include "../../src/deluge/util/pack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Some upper bound for destination buffers. SysEx messages on the wire are
// bounded by the static incomingSysexBuffer (~1 KiB) plus a generous margin.
#define DST_MAX (16 * 1024)

static void check_unpack_7bit(const uint8_t* data, size_t size) {
	if (size > DST_MAX) {
		return;
	}
	uint8_t* src = (uint8_t*)malloc(size);
	uint8_t* dst = (uint8_t*)malloc(DST_MAX);
	if (!src || !dst) {
		abort();
	}
	memcpy(src, data, size);

	int32_t produced = unpack_7bit_to_8bit(dst, DST_MAX, src, (int32_t)size);
	if (produced < 0 || produced > DST_MAX) {
		abort();
	}

	free(src);
	free(dst);
}

static void check_unpack_rle(const uint8_t* data, size_t size) {
	if (size > DST_MAX) {
		return;
	}
	uint8_t* src = (uint8_t*)malloc(size);
	uint8_t* dst = (uint8_t*)malloc(DST_MAX);
	if (!src || !dst) {
		abort();
	}
	memcpy(src, data, size);

	int32_t produced = unpack_7to8_rle(dst, DST_MAX, src, (int32_t)size);
	// Negative return is the function's error signal and is fine.
	if (produced > DST_MAX) {
		abort();
	}

	free(src);
	free(dst);
}

static void check_roundtrip(const uint8_t* data, size_t size) {
	if (size == 0 || size > DST_MAX / 2) {
		return;
	}
	uint8_t* original = (uint8_t*)malloc(size);
	uint8_t* packed = (uint8_t*)malloc(DST_MAX);
	uint8_t* unpacked = (uint8_t*)malloc(DST_MAX);
	if (!original || !packed || !unpacked) {
		abort();
	}
	memcpy(original, data, size);

	int32_t packed_len = pack_8bit_to_7bit(packed, DST_MAX, original, (int32_t)size);
	if (packed_len > 0) {
		int32_t unpacked_len = unpack_7bit_to_8bit(unpacked, DST_MAX, packed, packed_len);
		if (unpacked_len < (int32_t)size || memcmp(original, unpacked, size) != 0) {
			abort();
		}
	}

	int32_t rle_len = pack_8to7_rle(packed, DST_MAX, original, (int32_t)size);
	if (rle_len > 0) {
		int32_t unpacked_len = unpack_7to8_rle(unpacked, DST_MAX, packed, rle_len);
		if (unpacked_len != (int32_t)size || memcmp(original, unpacked, size) != 0) {
			abort();
		}
	}

	free(original);
	free(packed);
	free(unpacked);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size == 0) {
		return 0;
	}
	// Use the first byte as a 3-way selector so libFuzzer covers each
	// entrypoint without us shelling out to separate harnesses.
	const uint8_t* rest = data + 1;
	size_t rest_size = size - 1;
	switch (data[0] & 0x03) {
	case 0:
		check_unpack_7bit(rest, rest_size);
		break;
	case 1:
		check_unpack_rle(rest, rest_size);
		break;
	default:
		check_roundtrip(rest, rest_size);
		break;
	}
	return 0;
}
