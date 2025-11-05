# MIDI Device Selection & Routing

## Overview

This feature allows users to select specific MIDI output devices for individual MIDI instruments and kit drum rows, enabling precise control over which MIDI devices receive data from each track.

## Features

### Device Selection
- **ALL Devices**: Send MIDI to all connected outputs (default behavior, maintains backward compatibility)
- **DIN MIDI**: Send only to the 5-pin DIN MIDI port
- **USB MIDI Devices**: Send to specific USB MIDI devices (individually selectable)

### Scope
- **MIDI Instruments**: Each MIDI instrument track can have its own output device selection
- **Kit Drum Rows**: Each MIDI drum row in a kit can route to a different MIDI device
- **Per-Song Persistence**: Device selections are saved with songs

## Technical Implementation

### Device Index System

The implementation uses a simple device index system (not bitmasks):
- `0`: ALL devices (broadcast to all connected MIDI devices)
- `1`: DIN MIDI port only
- `2+`: USB MIDI devices (index 2 = first USB device, index 3 = second USB device, etc.)

### Key Components

#### Data Models
- **`MIDIInstrument::outputDevice`** (`uint8_t`): Device selection for MIDI instrument tracks
- **`MIDIDrum::outputDevice`** (`uint8_t`): Device selection for kit drum MIDI rows

#### UI Components
- **`OutputDeviceSelection`**: Menu item for MIDI instrument device selection
  - Path: `gui/menu_item/midi/output_device_selection.cpp`
  - Accessible in MIDI instrument sound editor

- **`KitOutputDeviceSelection`**: Menu item for kit drum row device selection
  - Path: `gui/menu_item/midi/sound/kit_output_device_selection.cpp`
  - Accessible when editing a MIDI drum row in a kit

#### MIDI Engine
- **`MidiEngine::sendNote()`**: Enhanced to accept device index parameter
- **`MidiEngine::sendMidi()`**: Enhanced to accept device index parameter
- **`MidiEngine::sendUsbMidi()`**: Enhanced to route to specific USB devices

Device routing logic:
```cpp
// DIN MIDI: Send if deviceFilter is 0 (ALL) or 1 (DIN)
if (deviceFilter == 0 || deviceFilter == 1) {
    dinCable.sendMessage(message);
}

// USB MIDI:
if (deviceFilter == 0) {
    // Send to ALL USB devices
} else if (deviceFilter == 1) {
    // DIN only - skip USB
} else {
    // Send to specific USB device (deviceFilter - 2 = USB index)
    uint32_t usbIndex = deviceFilter - 2;
}
```

### Device Name Matching Helpers
- **`getDeviceNameForIndex()`**: Get the name of a device from its index
- **`findDeviceIndexByName()`**: Find device index by matching name (with fallback to index)

These functions ensure reliable device matching when USB devices are reconnected in different orders.

### File Serialization

The system stores both the device **index** and **name** for reliable device matching when devices are reconnected in different orders.

#### MIDI Instrument
```xml
<outputDevice device="1" name="DIN" />
```

#### MIDI Drum
```xml
<midiOutput channel="0" note="36" outputDevice="2" outputDeviceName="Elektron Digitakt" />
```

### Device Reconnection & Name Matching

**Problem Solved:** USB devices can be plugged in different orders, which would change their indices.

**Solution:** When loading a song:
1. **First**, try to match by device name (e.g., "Elektron Digitakt")
2. **If name matches**, use that device regardless of its current index
3. **If name doesn't match**, fall back to the saved index (backward compatibility)

**Example:**
```
Original Setup:
  USB Device 1 = "Elektron Digitakt" → outputDevice=2
  USB Device 2 = "Arturia Keystep"  → outputDevice=3

After Reconnecting (different order):
  USB Device 1 = "Arturia Keystep"  → was index 3
  USB Device 2 = "Elektron Digitakt" → was index 2

When loading the song:
  - Looks for "Elektron Digitakt" by name
  - Finds it at new index 3
  - Correctly routes to the right device! ✅
```

## User Interface

### 7-Segment Display
Shows scrolling text with the selected device name (e.g., "ALL", "DIN", "USB 1")

### OLED Display
Shows a checkbox list of available devices with the currently selected device checked:
```
☑ ALL
☐ DIN
☐ USB 1
☐ USB 2
```

## Backward Compatibility

- Default value of `0` (ALL devices) maintains existing behavior
- Songs saved without device selection automatically use ALL devices
- No breaking changes to existing MIDI functionality

## Files Modified

### Core Implementation
- `src/deluge/model/instrument/midi_instrument.h` - Added `outputDevice` and `outputDeviceName` fields
- `src/deluge/model/instrument/midi_instrument.cpp` - Added serialization with name matching
- `src/deluge/model/drum/midi_drum.h` - Added `outputDevice` and `outputDeviceName` fields
- `src/deluge/model/drum/midi_drum.cpp` - Added serialization with name matching

### MIDI Engine
- `src/deluge/io/midi/midi_engine.h` - Added device index parameter
- `src/deluge/io/midi/midi_engine.cpp` - Implemented device routing logic
- `src/deluge/io/midi/midi_device_helper.h` - Helper functions for device name matching

### UI Components
- `src/deluge/gui/menu_item/midi/output_device_selection.h` - MIDI instrument menu
- `src/deluge/gui/menu_item/midi/output_device_selection.cpp` - MIDI instrument menu (stores device name)
- `src/deluge/gui/menu_item/midi/sound/kit_output_device_selection.h` - Kit drum menu
- `src/deluge/gui/menu_item/midi/sound/kit_output_device_selection.cpp` - Kit drum menu (stores device name)
- `src/deluge/gui/ui/menus.cpp` - Menu integration

## Usage Example

### Setup
1. Connect multiple MIDI devices (e.g., DIN synth + USB device)
2. Create MIDI instrument tracks for each device
3. Navigate to MIDI instrument menu → Output Device
4. Select desired output device (ALL, DIN, or specific USB device)

### Kit Row Routing
1. Create a kit with MIDI drums
2. Select a MIDI drum row
3. Navigate to sound editor → MIDI menu → Output Device
4. Select device for that specific drum

### Result
Each track/drum sends MIDI only to its selected device, allowing multi-device setups with independent routing.

## Benefits

- **Precise Routing**: Send specific tracks to specific devices
- **Multi-Device Workflows**: Use multiple MIDI devices simultaneously
- **Reduced MIDI Clutter**: Prevent unwanted MIDI messages on devices
- **Flexibility**: Mix devices freely (e.g., drums to one device, melody to another)
- **Per-Row Control**: In kits, route each drum sound independently

## Code Quality Improvements

- **Simple Design**: Uses straightforward device indices instead of complex bitmasks
- **Clean Serialization**: Minimal XML tags, only saves non-default values
- **Type Safety**: Uses `uint8_t` for device indices with clear bounds
- **Documentation**: Comprehensive inline comments and API documentation
- **Maintainability**: Clear separation of concerns between UI, data model, and MIDI engine
- **Robust Device Matching**: Stores device names for reliable matching when devices are reconnected
- **Backward Compatible**: Falls back to index-based matching for old songs without device names

