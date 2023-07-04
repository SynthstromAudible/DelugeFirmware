#include "definitions.h"

namespace HIDSysex {
void requestOLEDDisplay(int ip, int d, int cable, uint8_t* data, int len);
void sysexReceived(int ip, int d, int cable, uint8_t* data, int len);
void sendOLEDData(int ip, int d, int cable, bool rle);

} // namespace HIDSysex
