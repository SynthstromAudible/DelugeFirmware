# DOTT Multiband Compressor Testing Guide

## TL;DR

DOTT is a 3-band upward/downward compressor (OTT-style) with extensive tonal shaping via CHARACTER and VIBE zones. It can function as a traditional downward compressor, an upward expander, or anywhere in between.

**Quick Start:**
1. Load a synth with dynamics (pads, keys, or drums work well. one of my favs is sinewave sub + bit crush distortion)
2. FX menu → select DOTT
3. Adjust Ratio, threshold, and up/down skew to balance upward vs downward compression

**Suggested Test Patch:**
- Use a sine sub with bitcrush with a dynamic sequence or nonstatic note input
- Start with SKEW at 12:00 (balanced up/down)
- VIBE/FEEL at default

## Step-by-Step Walkthrough

### 1. Basic Setup
1. Load a test synth patch
2. Open FX menu → select DOTT
3. Set RATIO to ~50% for audible compression
4. Set THRESHOLD around 40% to catch peaks
5. Leave SKEW at center (50%) for balanced up/down compression

### 2. Explore CHARACTER Zones
The CHARACTER knob sweeps through 8 distinct compression personalities:

| Zone | Name | What to Listen For |
|------|------|-------------------|
| 0 | Width | Stereo field changes, bass bandwidth oscillation |
| 1 | Timing | Per-band attack/release variations |
| 2 | Skew | Tonal shifts from compression direction changes |
| 3 | Punch | Fast transients emphasized |
| 4 | Air | Glue-like sustain, highs lifted |
| 5 | Rich | Warm sustain, upward compression emphasis |
| 6 | OTT | Classic aggressive multiband character |
| 7 | OWLTT | Experimental phi-triangle oscillations |
(with push+twist offset, the whole range is free parameter evolution)

Try each zone with the same audio to hear the differences.

### 3. Explore VIBE Zones
VIBE controls phase relationships between band oscillations:

| Zone | Name | Effect |
|------|------|--------|
| 0 | Sync | All bands move together |
| 1 | Spread | Bands 120° out of phase |
| 2 | Pairs | Alternating phase relationships |
| 3 | Cascade | Progressive phase shift |
| 4 | Invert | Opposite phases between parameters |
| 5 | Pulse | Clustered phase effects |
| 6 | Drift | Slowly varying relationships |
| 7 | Twist | Phi-power frequency evolution |
(with push+twist offset, the whole range is free parameter evolution)

### 4. Explore SKEW (Up/Down Balance)
1. Set SKEW fully CCW → hear upward compression (quiet parts boosted)
2. Set SKEW fully CW → hear downward compression (loud parts reduced)
3. Set SKEW at 12:00 → balanced OTT-style (both active)
4. Notice how different CHARACTER zones interact with SKEW

### 5. Adjust Per-Band Controls
Navigate to per-band submenus:
1. **Low/Mid/High Threshold** - independent per-band triggering
2. **Low/Mid/High Ratio** - how hard each band compresses
3. **Low/Mid/High Output** - band level trim (+/-20dB)
4. **Crossover Low** - bass/mid boundary (50Hz-2kHz)
5. **Crossover High** - mid/treble boundary (200Hz-8kHz)

### 6. Watch the Meters
On OLED, the header shows real-time metering:
- **3 band meters**: Left pixel = output level, Right pixel = gain reduction
- **Gain reduction**: Above center = upward boost, Below = downward reduction
- **Master meter**: Overall output level
- **Clip dot**: Lights when clipping

## Access Points

- **FX Menu** → DOTT
- **Per-band controls** in Low/Mid/High submenus

## Parameters

### Global Controls

| Parameter | Range | Description |
|-----------|-------|-------------|
| CHARACTER | 8 zones | Compression personality (Width→OWLTT) |
| VIBE | 8 zones | Phase relationships between bands |
| SKEW | CCW-CW | Upward vs downward balance |
| THRESHOLD | 0-50 | Global compression threshold |
| RATIO | 0-50 | Compression intensity |
| ATTACK | 0.5-100ms | How fast compression engages |
| RELEASE | 5-500ms | How fast compression releases |
| OUTPUT | -inf to +16dB | Master output gain |
| BLEND | 0-50 | Wet/dry mix |

### Per-Band Controls

| Parameter | Description |
|-----------|-------------|
| Threshold | Band-specific trigger level |
| Ratio | Band-specific compression amount |
| Output | Band level trim (-inf to +20dB) |

### Crossover

| Parameter | Range | Description |
|-----------|-------|-------------|
| Low Crossover | 50Hz-2kHz | Bass/mid boundary |
| High Crossover | 200Hz-8kHz | Mid/treble boundary |

## Testing Scenarios

### 1. Basic Compression
- [ ] Enable DOTT → hear compression on dynamic material
- [ ] Increase RATIO → more aggressive limiting
- [ ] Lower THRESHOLD → more signal compressed
- [ ] Adjust ATTACK → fast catches transients, slow lets them through

### 2. VIBE Zone Sweep
- [ ] Slowly sweep VIBE → hear 8 distinct personalities (response depends on other settigns and input material)
- [ ] Zone 6 (OTT) → classic aggressive sound
- [ ] Zone 7 (OWLTT) → experimental evolving compression
- [ ] Each zone changes stereo width, timing, and tonal balance

### 3. FEEL Interaction
- [ ] With CHARACTER set, sweep VIBE → hear phase modulation
- [ ] Sync (zone 0) → coherent pumping
- [ ] Spread (zone 1) → bands breathe independently
- [ ] Twist (zone 7) → complex phi-triangle evolution
- [ ] Some FEEL settings might have more or less effect for input material, vibe settings or other compressor settings

### 4. SKEW Behavior
- [ ] SKEW CCW → upward compression dominates (quiets boosted)
- [ ] SKEW CW → downward compression dominates (louds reduced)
- [ ] SKEW center → balanced OTT effect
- [ ] Watch meters: upward shows gain above center line

### 5. Per-Band Independence
- [ ] Boost High Output → brighter compressed sound
- [ ] Lower Low Threshold → bass compresses more
- [ ] Adjust crossovers → change band boundaries
- [ ] Each band can have different ratio/threshold

### 6. Metering Accuracy
- [ ] Play loud material → see downward GR (below center)
- [ ] Play quiet material → see upward GR (above center)
- [ ] Clip dot lights on master clipping
- [ ] Band meters show independent activity

### 7. Extreme Settings
- [ ] RATIO 100% + low THRESHOLD → hard limiting
- [ ] SKEW full CCW + high RATIO → extreme upward expansion
- [ ] OWLTT zone + Twist vibe → maximum chaos
- [ ] OUTPUT boost can cause clipping (watch clip indicator)

## Secret Menus

Push+twist on FEEL/VIBE encoder reveals phase offset:
- **Vibe Phase Offset** → shifts all oscillations
- When > 0: full phi-triangle evolution across ALL zones
- Display shows "P:Z" coordinates instead of zone names

Encoder click on crossover selector enables per band and output soft clipping

## Known Behaviors

1. **CPU usage**: Higher with metering enabled; disable for performance
2. **Crossover types**: Multiple algorithms available (LR2, LR4, quirky modes)
3. **Band interaction**: Bands can mask each other at extreme settings
4. **Gain staging**: Watch output levels; easy to clip with heavy upward compression
5. **OWLTT zone**: Uses phi-triangle oscillations - constantly evolving character
