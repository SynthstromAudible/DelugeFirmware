// In-memory file transport for the production deserializers. No parsing is mocked.
#include "storage/storage_manager.h"

FileReader::FileReader() : FileReader(nullptr, 0) {
}
FileReader::FileReader(char* bytes, uint32_t size) {
	fileClusterBuffer = bytes;
	currentReadBufferEndPos = size;
	memoryBased = true;
	callRoutines = false;
	resetReader();
}
FileReader::~FileReader() = default;
void FileReader::resetReader() {
	fileReadBufferCurrentPos = 0;
	readCount = 0;
	reachedBufferEnd = false;
}
bool FileReader::readFileClusterIfNecessary() {
	reachedBufferEnd = fileReadBufferCurrentPos >= currentReadBufferEndPos;
	return !reachedBufferEnd;
}
bool FileReader::peekChar(char* byte) {
	if (!readFileClusterIfNecessary())
		return false;
	*byte = fileClusterBuffer[fileReadBufferCurrentPos];
	return true;
}
bool FileReader::readChar(char* byte) {
	if (!peekChar(byte))
		return false;
	++fileReadBufferCurrentPos;
	return true;
}
void FileReader::readDone() {
}
FRESULT FileReader::closeWriter() {
	return FR_OK;
}

// File-system configuration and diagnostic output used by both parsers.
#include "storage/cluster/cluster.h"
#include "util/firmware_version.h"
size_t Cluster::size = 32768;
FirmwareVersion song_firmware_version = FirmwareVersion::current();
namespace AudioEngine {
void logAudioAction(char const*, char const*, int) {
}
} // namespace AudioEngine
