#include "CppUTest/TestHarness.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"

AudioFile* AudioFileManager::getAudioFileFromFilename(String* filePath, bool mayReadCard, uint8_t* error,
                                                      FilePointer* suppliedFilePointer, AudioFileType type,
                                                      bool makeWaveTableWorkAtAllCosts) {
	FAIL("Not yet implemented");
	return nullptr;
}

int32_t AudioFileManager::getUnusedAudioRecordingFilePath(String* filePath, String* tempFilePathForRecording,
                                                          AudioRecordingFolder folder, uint32_t* getNumber) {
	FAIL("Not yet implemeted");
	return 0;
}

void AudioFileManager::loadAnyEnqueuedClusters(int32_t maxNum, bool mayProcessUserActionsBetween) {
	FAIL("not yet implemented");
}

bool AudioFileManager::loadCluster(Cluster* cluster, int32_t minNumReasonsAfter) {
	FAIL("not yet implemented");
	return false;
}
