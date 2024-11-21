#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"

namespace HIDSysex {
void requestOLEDDisplay(MIDICable& cable, uint8_t* data, int32_t len);
void request7SegDisplay(MIDICable& cable, uint8_t* data, int32_t len);
void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);
void sendOLEDData(MIDICable& cable, bool rle);
void sendOLEDDataDelta(MIDICable& cable, bool force);
void send7SegData(MIDICable& cable);
void sendDisplayIfChanged();
void readBlock(MIDICable& cable);
} // namespace HIDSysex
