#include "storage/flash_storage.h"

// TODO: add ways to set this stuff

namespace FlashStorage {

uint8_t defaultScale;
bool audioClipRecordMargins;
KeyboardLayout keyboardLayout;
uint8_t recordQuantizeLevel;
uint8_t sampleBrowserPreviewMode;
uint8_t defaultVelocity;
int8_t defaultMagnitude;

bool settingsBeenRead;
uint8_t ramSize;

uint8_t defaultBendRange[2] = {2, 48};

SessionLayoutType defaultSessionLayout;
KeyboardLayoutType defaultKeyboardLayout;

bool gridUnarmEmptyPads;
bool gridAllowGreenSelection;
GridDefaultActiveMode defaultGridActiveMode;

uint8_t defaultMetronomeVolume;

} // namespace FlashStorage
