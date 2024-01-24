//doing the minimal amount possible to not break

// this stub fails to allocate - uses to be required, but currently isn't.
// Take advantage of that to ensure anything which allocates will fail to link
// void* _sbrk(int incr) {
// 	return (void*)-1;
// }

void _exit(int status) {
	//halt execution
	__asm("BKPT #0");
	__builtin_unreachable();
}

//no return so just do nothing
void _kill(int pid, int sig) {
	return;
}

//fail to get current pid
int _getpid(void) {
	return -1;
}

//fail to close file
// int _close(int file) {
// 	return -1;
// }

// return character oriented (not block)
// int _fstat(int file, struct stat* st) {
// 	st->st_mode = S_IFCHR;
// 	return 0;
// }

// return character oriented
// int _isatty(int file) {
// 	return 1;
// }

// fail
// int _lseek(int file, int ptr, int dir) {
// 	return 0;
// }
// write nothing
// int _write(int file, char* ptr, int len) {
// 	return 0;
// }
// read nothing
// int _read(int file, char* ptr, int len) {
// 	return 0;
// }
