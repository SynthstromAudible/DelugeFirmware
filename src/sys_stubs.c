#include "definitions.h"
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

// System call stubs required for newlib (C library for embedded ARM targets)
// These provide minimal implementations of syscalls that the hardware doesn't provide by default
void* _sbrk(ptrdiff_t incr) {
	extern char _end; // Defined by the linker
	static char* heap_end;
	char* prev_heap_end;

	if (heap_end == 0) {
		heap_end = &_end;
	}
	prev_heap_end = heap_end;
	heap_end += incr;
	return (void*)prev_heap_end;
}

// needed for libc abort, raise, return from main
void _exit(int status) {
	// halt execution
	FREEZE_WITH_ERROR("EXIT");
	__builtin_unreachable();
}

// needed for libc abort
void _kill(int pid, int sig) {
	return;
}

// needed for libc abort
int _getpid(void) {
	return -1;
}

// needed for stdio, files
int _close(int file) {
	return -1;
}

// return character oriented (not block)
int _fstat(int file, struct stat* st) {
	st->st_mode = S_IFCHR;
	return 0;
}

// return character oriented
int _isatty(int file) {
	return 1;
}

int _lseek(int file, int ptr, int dir) {
	return 0;
}
// Minimal _write implementation - discard output but return length
int _write(int file, char* ptr, int len) {
	// If you want to send output to UART, do it here.
	return len;
}
// read nothing
int _read(int file, char* ptr, int len) {
	return 0;
}
