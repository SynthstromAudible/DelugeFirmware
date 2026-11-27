#ifndef DELUGE_SECTION_REORDER_TEST_PLAYBACK_MODE_SESSION_H
#define DELUGE_SECTION_REORDER_TEST_PLAYBACK_MODE_SESSION_H
#include <cstdint>
struct Session {
	uint8_t lastSectionArmed;
};
extern Session session;

#endif
