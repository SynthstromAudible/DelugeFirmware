#include "processing/engines/audio_engine.h"
#include <iostream>
using namespace std;
#define SHOW_AUDIO_LOG 0
void AudioEngine::logAudioAction(char const* string, const char* file, int line) {
#if SHOW_AUDIO_LOG
	cout << file << ":" << line << ": " << item << endl;
#endif
}

bool AudioEngine::bypassCulling;
