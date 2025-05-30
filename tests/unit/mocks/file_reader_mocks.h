#pragma once

#include "definitions_cxx.hpp"

class FileReader {
public:
	char readFrom[1024];
	char* fileClusterBuffer;
	int fileSize;
	bool readLine(char* thisLine) {
		if (fileSize <= 0) {
			return false;
		}
		while (*fileClusterBuffer != '\n') {
			fileClusterBuffer++;
			fileSize--;
		}
		if (fileSize >= 0) {
			*fileClusterBuffer = '\0';
			fileClusterBuffer++;
			fileSize--;
		}
		return true;
	}
	void resetReader() {}
	bool memoryBased = true;
	int fileReadBufferCurrentPos;
	int currentReadBufferEndPos;
};
