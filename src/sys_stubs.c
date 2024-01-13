#include "definitions.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
//doing the minimal amount possible to not break

void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam);

void* _sbrk(int incr) {
	FREEZE_WITH_ERROR("ESBRK");
	static unsigned char* heap = NULL;
	static unsigned char* end_heap = NULL;
	unsigned char* prev_heap;

	if (heap == NULL) {
		heap = delugeAlloc(10000, false);
		end_heap = heap + 10000;
	}
	prev_heap = heap;

	heap += incr;
	if (heap < end_heap) {
		return prev_heap;
	}
	else {
		heap = prev_heap;
		return (void*)-1;
	}
}

int _close(int file) {
	return -1;
}

int _fstat(int file, struct stat* st) {
	st->st_mode = S_IFCHR;
	return 0;
}

int _isatty(int file) {
	return 1;
}

int _lseek(int file, int ptr, int dir) {
	return 0;
}

void _exit(int status) {
	__asm("BKPT #0");
}

void _kill(int pid, int sig) {
	return;
}

int _getpid(void) {
	return -1;
}

int _write(int file, char* ptr, int len) {
	return 0;
}

int _read(int file, char* ptr, int len) {
	return 0;
}
