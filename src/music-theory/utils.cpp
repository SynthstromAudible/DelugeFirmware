#include "music-theory/utils.h"

namespace deluge::music {

bool isSameNote(int16_t a, int16_t b) {
	return (a - b) % 12 == 0;
}

} // namespace deluge::music
