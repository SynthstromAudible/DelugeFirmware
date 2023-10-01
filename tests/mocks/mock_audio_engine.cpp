#include "processing/engines/audio_engine.h"
#include <iostream>
using namespace std;

void AudioEngine::logAction(char const* item) {
	cout << item << endl;
}

bool AudioEngine::bypassCulling;
