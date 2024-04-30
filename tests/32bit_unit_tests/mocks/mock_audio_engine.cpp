#include "processing/engines/audio_engine.h"
#include <iostream>
using namespace std;
#define SHOW_AUDIO_LOG 0
void AudioEngine::logAction(char const* item) {
#if SHOW_AUDIO_LOG
	cout << item << endl;
#endif
}

bool AudioEngine::bypassCulling;
