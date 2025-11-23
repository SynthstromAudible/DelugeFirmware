# Clip Visualizer Implementation Plan

## Overview

Enhance the existing visualizer system to support displaying clip-specific waveforms when the user is actively viewing/editing a Synth or Kit clip in the **standard instrument clip view only** (not automation view or other clip view modes). The visualizer will show only the audio from the specific clip (post-effects) rather than the full mix, and will always use Waveform mode in clip view. When entering a clip, the program name for the clip's instrument will be displayed in a popup briefly.

**Important Note**: The clip visualizer should only be displayed in standard instrument view for clips and not automation or other types - only standard instrument clip view.

## Current Visualizer Architecture

### Key Components
- **Visualizer Class** (`src/deluge/hid/display/visualizer.h/cpp`): Main visualizer logic with multiple modes (Waveform, Spectrum, etc.)
- **Audio Sampling**: Happens in `AudioEngine::renderAudio()` after `renderSongFX()` - samples the full mix
- **Display Logic**: `potentiallyRenderVisualizer()` checks conditions and renders appropriate mode
- **Hotkey Control**: VU meter toggle via shift+level/pan mod encoder button controls visualizer enable/disable globally (VU meter display itself is only available in Song and Arranger views)

### Current Audio Sampling Flow
```
AudioEngine::renderAudio()
├── renderSongFX()  // Applies global effects
├── sampleAudioForDisplay()  // Samples full mix for visualizer
└── Output to speakers
```

## Proposed Changes

### 1. Clip-Specific Audio Sampling

**Location**: `GlobalEffectableForClip::renderOutput()` in `src/deluge/model/global_effectable/global_effectable_for_clip.cpp`

**Hook Point**: After `processFXForGlobalEffectable()` (line 133-134), where clip audio has gone through all effects but before final mixing.

**Thread Safety Pattern**: Follow same pattern as full mix sampling - audio thread calls sampling function unconditionally, but actual sampling is gated by UI state checks (similar to `isEnabled()` check in full mix).

**Current Clip Access**: Use an atomic pointer to the current clip for visualization, updated by the UI thread when entering clip view. Audio thread compares `this` clip with the atomic current clip pointer.

**New Sampling Function**: Add clip-specific sampling that:
- Audio thread calls sampling function for every clip render
- Actual sampling gated by: visualizer enabled AND in clip view AND `this` clip matches current clip for visualization AND current clip is Synth/Kit
- Uses same sampling pattern as full mix (every 2nd sample, Q31→Q15 conversion)
- Stores samples in clip-specific buffer (reuse existing buffer to save memory)
- Only samples Synth and Kit clips (not Audio/MIDI clips)

### 2. Clip View Detection

**Detection Logic**:
```cpp
bool isInClipView = (currentUI == &instrumentClipView);

bool isSynthOrKitClip = (clip->type == ClipType::INSTRUMENT &&
                         (clip->output->type == OutputType::SYNTH ||
                          clip->output->type == OutputType::KIT));
```

**UI States**: Active when user is in InstrumentClipView with a Synth/Kit clip.

### 3. Visualizer Mode Restrictions

**Clip Mode Behavior**:
- Only Waveform mode available in clip view
- Force visualizer into Waveform mode when entering clip view
- Users cannot change visualizer mode when in clip view - it is always Waveform

**Mode Detection**: Add `isClipMode()` function to determine if visualizer should show clip audio vs. full mix.

### 4. Hotkey Integration

**Existing Hotkey**: Shift + Level/Pan mod encoder button (MOD_ENCODER_0/MOD_ENCODER_1 with whichButton == 0)

**Current Logic**: Located in `View::modEncoderButtonAction()` - toggles `displayVUMeter` which controls visualizer.

**Clip View Behavior**: While the hotkey will work in clip mode since it uses the same `displayVUMeter` flag, note that the VU meter's automatic display behavior is irrelevant to clip view. The VU meter itself cannot be used in clip view - it is only available in Song and Arranger views. The visualizer in clip view is controlled by the same global flag but functions independently of VU meter display logic.

### 5. Buffer Management

**Buffer Reuse Strategy**:
- Since only one visualizer can be displayed at a time, reuse existing buffers to save memory
- Use same buffer arrays for both full-mix and clip visualization
- Clear/reset buffers when switching between modes to avoid displaying stale data

**Buffer Synchronization**:
- Clear buffer contents when switching from session to clip mode or between clips
- Visualizer logic automatically uses appropriate buffer contents based on current mode
- No separate clip-specific buffers needed
- Maintain same circular buffer logic

### 6. State Management

**Clip Visualizer State**:
- Add `clip_visualizer_active` flag to track when clip-specific visualizer is running
- Add atomic `current_clip_for_visualizer` pointer to track which clip is being visualized (thread-safe access)
- Reset clip visualizer state when switching clips or exiting clip view
- Update current clip pointer when entering clip view

**Session Persistence**:
- Clip visualizer enable/disable state should persist globally (not per clip)
- Use existing `displayVUMeter` flag (shared with full mix visualizer)
- Note: While the same flag is used, the VU meter's automatic display behavior does not apply to clip view since VU meters are only shown in Song and Arranger views
- Visualizer mode is not remembered separately for clip view - clips always use Waveform mode

### 7. Program Name Popup Display

**Popup Trigger**:
- Display popup every time user enters a clip (if visualizer is active)
- Show instrument program/preset name similar to visualizer mode change popups
- Use `display->displayPopup()` with instrument name from `output->name.get()`

**Display Logic**:
- Show popup every time user enters a clip (when visualizer is active)
- Display clip's instrument program name briefly for user feedback
- Reset popup state when exiting clip view

### 8. Implementation Steps

#### Phase 1: Core Sampling Infrastructure
1. Add `sampleAudioForClipDisplay()` function with same logic as `sampleAudioForDisplay()`
2. Hook into `GlobalEffectableForClip::renderOutput()` after effects processing
3. Add clip view detection logic and UI state tracking

#### Phase 2: Visualizer Logic Updates
1. Add `isClipMode()` and `isClipVisualizerActive()` functions
2. Modify `potentiallyRenderVisualizer()` to use clip buffers when in clip mode
3. Force Waveform mode when in clip mode (no mode switching available for clips)
4. Add clip visualizer state management
5. Add program name popup display when entering clip visualizer mode

#### Phase 3: UI Integration
1. Update view transition logic to handle clip visualizer state
2. Ensure hotkey works in clip views
3. Add proper cleanup when exiting clip views

#### Phase 4: Testing & Refinement
1. Test with Synth clips in instrument clip view
2. Test with Kit clips in instrument clip view
3. Verify hotkey functionality
4. Test mode switching (session ↔ clip view)
5. Performance testing

### 9. Files to Modify

**Core Visualizer Files**:
- `src/deluge/hid/display/visualizer.h` - Add new functions for clip mode detection and atomic current clip pointer
- `src/deluge/hid/display/visualizer.cpp` - Implement clip sampling, mode detection, popup display logic, buffer reuse, and current clip tracking

**Audio Processing**:
- `src/deluge/model/global_effectable/global_effectable_for_clip.cpp` - Add sampling hook

**UI/View Management**:
- `src/deluge/gui/views/view.cpp` - Update visualizer state management for clip transitions
- `src/deluge/gui/views/instrument_clip_view.cpp` - Handle clip visualizer state on view entry/exit

### 11. Backward Compatibility

**No Breaking Changes**:
- Full mix visualizer continues to work unchanged
- Existing hotkeys and settings preserved
- Clip visualizer is opt-in (only active when in clip view with Synth/Kit clips)

### 13. Performance Considerations

**CPU Impact**:
- Follow existing patterns used for full mix sampling
- Sampling only occurs when visualizer is enabled AND in clip view
- Same sampling rate as full mix (every 2nd sample)
- Only active for Synth/Kit clips, not Audio/MIDI clips

**Memory Impact**:
- Reuse existing buffers (no additional memory required)
- Total visualizer memory remains ~3KB

### 15. Edge Cases & Error Handling

**Clip Type Restrictions**:
- Only enable for Instrument clips with Synth/Kit outputs
- Disable for Audio clips and MIDI clips

**View Transitions**:
- Properly clean up clip visualizer state when switching between clips
- Handle view transitions (session ↔ arranger ↔ instrument clip view)
- Show program name popup when switching between clips

**Audio Engine States**:
- Respect existing visualizer enable/disable logic (global visualizer state, not VU meter display which is view-specific)
- Don't sample when visualizer feature is disabled in settings

**Popup Display**:
- Handle empty/null instrument names gracefully
- Don't interfere with other popup displays

### 17. Future Enhancements

**Potential Extensions** (not in scope for this implementation):
- Support for Audio clip visualization
- MIDI clip piano roll integration
- Clip-specific visualizer settings persistence

## Conclusion

This implementation provides clip-specific waveform visualization with a simplified approach - clips always use Waveform mode and users cannot change it. The solution maintains full backward compatibility and follows existing patterns while integrating seamlessly with the current visualizer architecture and hotkey system, and provides user feedback through program name popups when entering clip visualizer mode.
