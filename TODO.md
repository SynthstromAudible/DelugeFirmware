# MIDI Device Selection - Future Features

## Current Status
✅ **MIDIRouting Data Class Implementation** (Commit: b6474475)
- Implemented MIDIRouting data class with device/channel members
- Replaced bitmask approach with cleaner data structure
- Updated MIDIDrum to use outputRouting.toDeviceFilter()
- Enhanced file I/O to save/load device and channel attributes
- Updated UI components for new data class structure
- Successfully builds and ready for testing

## TODO Features

### 1. Save and Recall Output Device to Song File
- **Status**: Partially implemented (device selection saved, but needs device name matching)
- **Description**: When saving songs, store the selected output device. When loading songs, match the saved device name to current connected devices and fall back gracefully if device is no longer connected.
- **Implementation**:
  - Store device names in song files for USB devices
  - Implement device name matching on load
  - Graceful fallback to "ALL devices" if saved device not found
  - Maintain backward compatibility with older song files


## Technical Notes
- Current implementation uses MIDIRouting data class for cleaner architecture
- File I/O already supports device and channel attributes
- UI components updated to work with new data structure
- Build system confirmed working with new implementation

## Related Files
- `src/deluge/io/midi/midi_routing.h` - New data class
- `src/deluge/model/drum/midi_drum.h/cpp` - Updated MIDI drum implementation
- `src/deluge/gui/menu_item/midi/` - Updated UI components
