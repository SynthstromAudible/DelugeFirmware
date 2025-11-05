# Device Name Matching Solution

## Problem
USB MIDI devices can be plugged in different orders, causing their device indices to change. A song that was set to route to "Elektron Digitakt" at index 2 would route to the wrong device if the Digitakt is now at index 3.

## Solution
Store both the **device index** AND **device name** in song files. When loading, match by name first (more reliable), then fall back to index for backward compatibility.

## How It Works

### 1. Saving (Writing)
When user selects a device in the UI:
- Store the **device index** (0-255)
- Look up and store the **device name** (e.g., "Elektron Digitakt")
- Save both to file

**XML Output:**
```xml
<!-- MIDI Instrument -->
<outputDevice device="2" name="Elektron Digitakt" />

<!-- MIDI Drum -->
<midiOutput channel="0" note="36" outputDevice="2" outputDeviceName="Elektron Digitakt" />
```

### 2. Loading (Reading)
When loading a song:
1. Read both `device` index and `name` from file
2. Try to find a device with matching `name`
3. If found, use that device's **current** index (even if different from saved index)
4. If not found, fall back to the saved index (backward compatibility)

**Example:**
```cpp
// In MIDIDrum::readFromFile()
if (!savedDeviceName.isEmpty()) {
    // Try to match by name first
    outputDevice = findDeviceIndexByName(savedDeviceName.get(), savedOutputDevice);
} else {
    // Fall back to index only (old songs without names)
    outputDevice = savedOutputDevice;
}
```

## Real-World Example

### Initial Setup
```
Connected Devices:
  Index 1: DIN
  Index 2: Elektron Digitakt
  Index 3: Arturia Keystep

Song saved with drum routed to "Elektron Digitakt" (index 2)
  outputDevice=2
  outputDeviceName="Elektron Digitakt"
```

### After Reconnecting Devices (Different Order)
```
Connected Devices:
  Index 1: DIN
  Index 2: Arturia Keystep  ← was 3
  Index 3: Elektron Digitakt ← was 2

When loading the song:
  1. Reads: device=2, name="Elektron Digitakt"
  2. Searches for device named "Elektron Digitakt"
  3. Finds it at NEW index 3
  4. Routes correctly to Elektron Digitakt! ✅
```

## Implementation Details

### Helper Functions (`midi_device_helper.h`)

```cpp
// Get device name from index
std::string_view getDeviceNameForIndex(uint8_t deviceIndex);

// Find device index by name (with fallback)
uint8_t findDeviceIndexByName(std::string_view deviceName, uint8_t fallbackIndex);
```

### Data Structures

**MIDIInstrument:**
```cpp
uint8_t outputDevice{0};      // 0=ALL, 1=DIN, 2+=USB
String outputDeviceName;       // "Elektron Digitakt", "DIN", etc.
```

**MIDIDrum:**
```cpp
uint8_t outputDevice{0};       // 0=ALL, 1=DIN, 2+=USB
String outputDeviceName;       // "Elektron Digitakt", "DIN", etc.
```

## Backward Compatibility

### Old Songs (Without Device Names)
- Still load correctly using index-based matching
- When saved again, will include device name for future robustness

### New Songs
- Always include both index and name
- Robust to device reconnection order

## Edge Cases Handled

1. **Device not found by name**
   - Falls back to saved index
   - Best effort to maintain functionality

2. **Empty device name**
   - Uses index-only matching
   - Maintains backward compatibility

3. **Device name changed**
   - Falls back to saved index
   - User can manually reselect correct device

4. **Multiple devices with same name**
   - Uses first match found
   - Unlikely in practice (USB devices have unique names/serial)

## Files Modified

### Core Implementation
- `src/deluge/model/instrument/midi_instrument.h` - Added `outputDeviceName`
- `src/deluge/model/instrument/midi_instrument.cpp` - Name-based loading/saving
- `src/deluge/model/drum/midi_drum.h` - Added `outputDeviceName`
- `src/deluge/model/drum/midi_drum.cpp` - Name-based loading/saving

### Helper Functions
- `src/deluge/io/midi/midi_device_helper.h` - NEW FILE
  - `getDeviceNameForIndex()` - Lookup device name
  - `findDeviceIndexByName()` - Match device by name

### UI Components
- `src/deluge/gui/menu_item/midi/output_device_selection.cpp` - Store device name on selection
- `src/deluge/gui/menu_item/midi/sound/kit_output_device_selection.cpp` - Store device name on selection

## Testing Checklist

- [ ] Create song with USB device selected
- [ ] Save song
- [ ] Disconnect/reconnect USB devices in different order
- [ ] Load song - verify correct device is selected
- [ ] Try with DIN device
- [ ] Try with ALL devices (should always work)
- [ ] Load old song without device names (backward compat)
- [ ] Verify fallback to index when name not found

## Benefits

✅ **Robust**: Survives device reconnection in different orders
✅ **Intuitive**: "Elektron Digitakt" routes to Elektron Digitakt, regardless of USB port
✅ **Backward Compatible**: Old songs still work
✅ **Future-Proof**: New songs automatically get robust matching
✅ **Minimal Overhead**: Only one additional string per track/drum

## Summary

This solution ensures that MIDI device routing is **stable and predictable** even when USB devices are connected in different orders. Users don't have to worry about device indices changing - the system intelligently matches by device name.

