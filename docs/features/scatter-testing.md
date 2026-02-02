# Scatter Effect Testing Guide

## TL;DR

Scatter is a beat-repeat/granular effect that captures audio into a buffer and plays it back with various transformations. Think of it as a sophisticated stutter effect with zone-based parameter control.

**Quick Start:**
1. Load a synth/kit with audio playing
2. Press stutter encoder → enters ARMED/STANDBY (recording)
3. Release and re-press → starts PLAYING with effect (should wait until the buffer is populated and trigger playback next beat)
4. Turn zone knobs to explore different transformations

**Suggested Test Patch:**
- Use an arp or patch that plays notes every 1/4 note (rhythmic content makes slice effects audible)
- Set scatter to **Shuffle** mode - easiest to hear the various parameter impacts
- Shuffle reorders slices based on phased phi-triangle evolution

## Step-by-Step Walkthrough

### 1. Basic Setup
1. Load an arp or rhythmic patch (notes every 1/4 note work well)
2. Set scatter rate to **1/16** (default) via the stutter menu
3. Set mode to **Shuffle**
4. Press the scatter gold encoder once to allocate buffers and arm recording (STANDBY)
5. Press again to trigger playback

### 2. Explore Pattern and Color (Page 2)
With default settings, Shuffle doesn't rearrange grains noticeably. To hear the effect:

1. Navigate to Page 2 of the stutter menu
2. Turn **Pattern** (Zone A) away from default - this controls slice ordering. effects are more dramatic/obvious at higher settings.
3. Turn **Color** (Zone B) away from default - this adds timbral variation (reverse, filter, envelope)
4. Explore both knobs - they control a very large parameter space
5. The zone names (Drift, Echo, Fold, etc.) represent loose tonal "themes" moreso than distinct modes - useful to remember as reference points

### 3. Explore Density
1. Turn **Density** down toward 0%
2. Hear more of the original dry signal coming through (grains fade out)
3. At 0%, you hear only the input audio
4. Reset Density to 50-100% for grain output

### 4. Explore Macro Config and Macro
1. With **Macro at 0%**, Macro Config does nothing - it's a modifier
2. Turn **Macro to 100%**
3. Now adjust **Macro Config** - hear how it changes the intensity and character
4. Find an interesting variation
5. Turn Macro back to 0% to hear the original pattern (Macro Config influence disappears)

### 5. Explore pWrite
**pWrite controls buffer content evolution:**
- **0% (CCW)**: Buffer is frozen - same audio loops forever
- **50% (CW)**: Buffer constantly overwrites with fresh incoming audio or resampled grains
- **Mid values**: Probabilistic overwrite - content morphs gradually

Try this with different Density settings:
- High Density + low pWrite = frozen glitch loop
- High Density + high pWrite = live stutter effect
- Low Density + low pWrite = dry signal over frozen background grains. Pattern zone parameters will control which grains are dry or wet.

### 6. Explore Other Modes
After understanding the basics in Shuffle mode, try:
- **Grain** - Granular cloud with dual crossfading voices, very different texture
- **Pitch** - Pitch manipulation with scale quantization, musical transpositions. "density" ui control sacrificed for scale control.
- **Pattern** - Slice patterns with structured sequencing
- **Time** - Direct control over grain length and slice hold
- **note** - grains written to the buffer are shared across modes, so you can for instance, inject scale-shifted grains in pitch mode using pwrite > 0, then use them in granular mode. There is also a takeover mechanism which shres the buffer between presets or different tracks allowing the buffer to accumulate multiple timbres from different patches. takeover happens while changin patch while scatter playing, or starting scatter playback on a new track while the original track is still playing. to discard the buffer and start over, stop playing and start again. The buffer is discarded and deallocated after 32 bars of inactivity (not playing).

## Access Points

- **Stutter encoder** (top right) - hold to arm, release+press to trigger
- **FX menu** → Stutter → Mode (select scatter mode)
- **Shortcut**: While in stutter menu, gold knob adjusts rate

## Modes

| Mode | Description |
|------|-------------|
| Classic | Original stutter (no scatter) |
| Burst | Gated stutter: play grain, silence until next-- a lot like classic without repitch |
| Repeat | Beat repeat with count control |
| Time | Zone A=grain length, Zone B=hold slice |
| Shuffle | Phi-based segment reordering |
| Grain | Granular cloud with dual crossfading voices |
| Pattern | Slice patterns (seq/weave/skip/mirror/pairs) |
| Pitch | Pitch manipulation with scale quantization |

## Parameters (Page 2)

### Zone A - "Pattern" (Structural)
Controls timing/ordering of slices:
- **Drift** → Sequential with slight offset
- **Echo** → Adjacent pair swapping
- **Fold** → Reverse order tendency
- **Leap** → Interleaved skipping
- **Weave/Spiral/Bloom/Void** → Meta evolution modes

### Zone B - "Color" (Timbral)
Controls sound character:
- **Rose/Blue/Indigo/Green** → Discrete timbral zones
- **Lotus/White/Grey/Black** → Meta evolution modes

### Macro Config - "Flavor" (Effect Depth)
Weather/nature themed intensity:
- **Frost** → Minimal effect
- **Dew/Fog/Cloud** → Light to medium
- **Rain/Storm** → Heavy processing
- **Dark/Night** → Maximum chaos

### Macro
Master intensity control (0-50). Modulates all zone effects.

### pWrite (Buffer Write Probability)
- **0%** (CCW) → Freeze buffer (never overwrite)
- **50%** (CW) → Always overwrite with fresh content

### Density (Grain Output)
- **0%** (CCW) → All dry output (hear input, no grains)
- **50%** (CW) → 100% grain playback

## Testing Scenarios

### 1. Basic Operation
- [ ] Press stutter encoder → shows "STANDBY" indicator
- [ ] Buffer records a rolling 1 bar buffer in the background
- [ ] re-press encoder → playback starts on next beat
- [ ] Double-tap while buffer not full → queues trigger, starts when ready

### 2. Mode Switching
- [ ] While playing, change mode in menu → seamless transition
- [ ] Cannot switch between Classic/Burst and looper modes during playback (different buffer systems)

### 3. Zone Parameters
- [ ] Turn Zone A → hear structural changes (slice reordering)
- [ ] Turn Zone B → hear timbral changes (reverse, filter, envelope)
- [ ] Push+twist any zone knob → adjusts phase offset (shows "offset:X")
- [ ] Push+twist Macro knob → adjusts gamma (global offset multiplier)

### 4. pWrite Behavior
- [ ] pWrite=0 → Buffer content freezes, same audio loops
- [ ] pWrite=50 → Buffer constantly updates with new audio
- [ ] Mid values → Probabilistic overwrite (morphing content)

### 5. Density Behavior
- [ ] Density=0 → Hear input audio, no grains
- [ ] Density=50 → Full grain output
- [ ] Mid values → Mix of dry and grains

### 6. Takeover (Track Switching)
- [ ] Start scatter on Track A
- [ ] Switch to Track B, trigger scatter → B inherits A's buffer if B starts playing while A is still playing
- [ ] B's zone values show A's inherited settings in this scenario
- [ ] pWrite controls how fast B's audio replaces A's

### 7. Preset Change During Scatter
- [ ] Start scatter playback, then load different preset
- [ ] Scatter continues with new preset's audio playing "under" the existing loop when density < 1
- [ ] New preset inherits previous scatter settings if switched while playing

### 8. Grain Mode Specifics
- [ ] Rate knob → grain size
- [ ] Zone A → position, spread
- [ ] Zone B → timbral character (pan, delay, pitch, dry)
- [ ] Stereo width (secret param) → voice A/B panning

### 9. Default Values (New Presets)
- [ ] pWrite defaults to 0% (freeze buffer)
- [ ] Macro defaults to 0% (no effect intensity)
- [ ] Density defaults to 100% (full grain output)

### 10. Mod Matrix (Synth Only)
- [ ] Press select on Macro/pWrite/Density → opens mod source selection
- [ ] Patch LFO to Macro → animated intensity
- [ ] map random lfo to pwrite, macro, or density for long, slowly evolving patches
- [ ] Patched params show dot on menu name

## Secret Menus

Push+twist encoder on zone params reveals phase offset adjustment:
- **Zone A encoder** → zoneAPhaseOffset
- **Zone B encoder** → zoneBPhaseOffset
- **Macro Config encoder** → macroConfigPhaseOffset
- **Macro encoder** → gammaPhase (multiplier for all offsets)

These basically add an offset to the positions of their respective knobs. "gamma" applies a large global offset for large jumps in parameter space.
Display shows "offset:X" or "gamma:X" while adjusting.

## Known Behaviors

1. **Beat quantization**: Playback starts on beat boundaries
2. **Buffer size**: ~4 seconds at 44.1kHz
3. **Half-bar mode**: Long bars capture 2 beats, virtually doubled
4. **Latched by default**: Looper modes stay on after encoder release
5. **Standby timeout**: 32 bars of idle releases buffer

## Commit History (Recent)

- `f00c7fdd` - Weather/nature names for macro config zones (abstract because the effects are predominatly determined by phi triangle phases)
- `49d16ea0` - Queue pending trigger even when buffer not full
- `adc18c12` - Fix macro zone param classification and defaults
- `14c3eda1` - Compute per-grain effects in grain mode
