#include <memory.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void* memmove(void*, const void*, size_t);

void* naive_memmove(void* dest, const void* src, size_t n) {
	char* d = dest;
	const char* s = src;

	if (s < d && d < (s + n)) { // Overlap, copy backwards
		d += n;
		s += n;
		while (n--) {
			*--d = *--s;
		}
	}
	else { // No overlap, copy forwards
		while (n--) {
			*d++ = *s++;
		}
	}

	return dest;
}

int LLVMFuzzerTestOneInput(const uint8_t* raw_data, size_t size) {
	if (size < 2) {
		return 0;
	}

	size_t len = raw_data[0] % size;
	size_t offset = raw_data[1] % (size - len);

	// Allocate memory for the buffers
	uint8_t* buf1 = (uint8_t*)malloc(size);
	uint8_t* buf2 = (uint8_t*)malloc(size);

	if (!buf1 || !buf2) {
		fprintf(stderr, "Error: Memory allocation failed\n");
		abort();
	}

	// Copy the raw data into the buffers
	memcpy(buf1, raw_data, size);
	memcpy(buf2, raw_data, size);

	// Call memmove and the custom my_memmove
	memmove(&buf1[offset], buf1, len);
	naive_memmove(&buf2[offset], buf2, len);

	// Compare the buffers for equality
	if (memcmp(buf1, buf2, size) != 0) {
		fprintf(stderr, "Error: memmove mismatch\n");

		printf("Len: %u\n", len);
		printf("Offset: %u\n", offset);

		printf("Source: ");
		for (int i = 0; i < size; i++) {
			printf("%02X ", raw_data[i]); // Print each byte in hexadecimal format
		}
		printf("\n");

		printf("Expect: ");
		for (int i = 0; i < size; i++) {
			printf("%02X ", buf2[i]); // Print each byte in hexadecimal format
		}
		printf("\n");

		printf("Actual: ");
		for (int i = 0; i < size; i++) {
			printf("%02X ", buf1[i]); // Print each byte in hexadecimal format
		}
		printf("\n");

		abort();
	}

	// Free the allocated memory
	free(buf1);
	free(buf2);

	return 0;
}
