#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"

namespace HIDSysex {
void requestOLEDDisplay(MIDIDevice* device, uint8_t* data, int32_t len);
void request7SegDisplay(MIDIDevice* device, uint8_t* data, int32_t len);
void sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
void sendOLEDData(MIDIDevice* device, bool rle);
void sendOLEDDataDelta(MIDIDevice* device, bool force);
void send7SegData(MIDIDevice* device);
void sendDisplayIfChanged();
} // namespace HIDSysex
