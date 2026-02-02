# Sine Shaper (HOOT) Testing Guide

## TL;DR

Sine Shaper is a harmonic waveshaping effect that adds musical saturation and overtones through various waveshaping algorithms. It uses zone-based Harmonic and Twist controls with phi-triangle blending for smooth, evolving timbres.

**Quick Start:**
1. Load a synth with simple waveforms (sine or triangle work best to hear harmonics)
2. FX menu → Distortion → Sine Shaper (HOOT)
3. Turn **Drive** up to add gain into the shaper (note, for chebyschev (1375) mode, it is best to not saturate the input--dial back gain when you hear clipping.)
4. Turn **Harmonic** to select waveshaping algorithm (8 zones)
5. Turn **Twist** to add modulation effects (width, feedback, rectification)

**Suggested Test Patch:**
- Use a simple sine or triangle oscillator (square does not work for wavefolding)
- Start with Drive around 50% to hear shaping clearly
- Harmonic zone 0 (3579) adds clean odd harmonics

## Step-by-Step Walkthrough

### 1. Basic Setup
1. Load a patch with simple harmonics (sine, triangle, or soft pad)
2. Open FX menu → Distortion → select Sine Shaper
3. Set **Drive** to ~50% (enough to hear shaping)
4. Set **Mix** to 100% (fully wet) to hear the effect clearly
5. Start with Harmonic and Twist at zone 0

### 2. Explore Harmonic Zones
The Harmonic knob selects between 8 waveshaping algorithms:

| Zone | Name | Character |
|------|------|-----------|
| 0 | 3579 | Clean odd harmonics (3rd, 5th, 7th, 9th) via Chebyshev |
| 1 | 3579wm | Warmer FM-like character (sine-fold-preprocessed to avoid clipping saturation) |
| 2 | FM | Mixed modes: Add, Ring, FM, Fold variations |
| 3 | Fold | Wavefolder at depths k=1,2,3,4 |
| 4 | Ring | Ring modulation sin(x)×sin(nx) |
| 5 | Add | Additive harmonics sin(x)+sin(nx) |
| 6 | Mod | FM waveshaping with varying depths |
| 7 | Poly | Cascaded polynomial waveshaping |

Sweep slowly with a simple input to hear each algorithm's character.

### 3. Explore Twist Zones
Twist adds modulation and processing effects:

| Zone | Name | Effect |
|------|------|--------|
| 0 | Width | Stereo spread with animated phase |
| 1 | Evens | Asymmetric compression for even harmonics |
| 2 | Rect | Dual rectifiers (absolute value, sine expansion) |
| 3 | Feedback | Output→input recirculation (capped at 25%) |
| 4-7 | Meta | All effects combined with phi-triangle evolution |

### 4. Drive and Mix Interaction
1. **Drive at 0%** → signal passes through unchanged
2. **Drive at 50%** → moderate saturation
3. **Drive at 100%** → maximum harmonic content
4. **Mix** blends wet/dry (0% = bypass, 100% = full effect)
5. Try low Drive + high Mix for subtle coloring

### 5. Combine Harmonic and Twist
Interesting combinations:
- **3579 + Rect** → rich stereo odd harmonics with grit
- **Fold + Feedback** → aggressive self-modulating distortion
- **Ring + Rect** → harsh, synthy character
- **Poly + Meta zones** → complex evolving timbre

### 6. Explore Meta Zones (Twist 4-7 and above)
When Twist is in zones 4-7:
- All effects (width, evens, rect, feedback) combine
- Each effect modulated by phi-triangle frequencies
- Position within zone controls overall intensity
- Creates continuously evolving harmonic content

## Access Points

- **FX Shaping Menu** → Distortion → Sine Shaper
- **Parameters**: Drive, Harmonic, Twist, Mix
- **Mod Matrix** (synth only): Press select on Drive/Mix for modulation routing

## Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Drive | 0-50 | Input gain (0-4x) |
| Harmonic | 0-50 (8 zones) | Waveshaping algorithm selection |
| Twist | 0-50 (8 zones) | Modulation effect selection |
| Mix | 0-50 | Wet/dry blend |

## Testing Scenarios

### 1. Basic Waveshaping
- [ ] Enable Sine Shaper with simple input → hear added harmonics
- [ ] Increase Drive → more saturation and harmonics
- [ ] Adjust Mix → blend with dry signal
- [ ] Bypass (Mix=0) → clean signal comparison

### 2. Harmonic Zone Sweep
- [ ] Zone 0 (3579) → clean odd harmonics
- [ ] Zone 3 (Fold) → wavefolder character
- [ ] Zone 4 (Ring) → metallic ring mod tones
- [ ] Zone 7 (Poly) → complex polynomial distortion
- [ ] Each zone has distinct harmonic fingerprint

### 3. Twist Effects
- [ ] Width (zone 0) → stereo spread audible in headphones
- [ ] Evens (zone 1) → asymmetric, even harmonic boost
- [ ] Rect (zone 2) → harsh rectification
- [ ] Feedback (zone 3) → self-oscillation at high settings

### 4. Meta Zone Evolution
- [ ] Twist zones 4-7 → all effects combined
- [ ] Sweep within meta zone → evolving character
- [ ] Notice phi-triangle modulation (non-repeating)
- [ ] Higher zone = more intense combination

### 5. Drive Staging
- [ ] Low Drive + 3579 → subtle harmonic enrichment
- [ ] High Drive + Fold → aggressive wavefolder
- [ ] Drive affects all algorithms differently
- [ ] Watch output level (can get loud)

### 6. Modulation (Synth Only)
- [ ] Press select on Drive → assign mod source
- [ ] LFO to Drive → animated saturation
- [ ] Envelope to Mix → dynamic wet/dry
- [ ] Patched params show dot indicator

### 7. Input Signal Dependency
- [ ] Sine input → cleanest harmonic addition
- [ ] Complex input → more aliasing/intermodulation
- [ ] Percussive input → transient shaping
- [ ] High-frequency content → may alias (by design)

## Secret Menus

Push+twist on encoders reveals phase offsets:

- **Push Twist encoder** → `twistPhaseOffset`
  - When > 0: ALL zones become meta (phi-triangle evolution)
  - Display shows "T:N" coordinates

- **Push Harmonic encoder** → `harmonicPhaseOffset`
  - Shifts harmonic zone cycling independently
  - Display shows "offset:N"

- **Push Mix encoder** → `gammaPhase`
  - Global phase offset multiplier (1024x scale)
  - Display shows "G:N"
  - Shifts entire parameter space

**Meta Mode Behavior:**
When any phase offset > 0:
- Numeric "p:z" display replaces zone names
- All parameters evolve via phi-triangle frequencies
- Creates infinite non-repeating timbral combinations

## Known Behaviors

1. **Chebyshev zones (0-1)**: Use polynomial extraction for clean odd harmonics
2. **Hard saturation**: Input bounded to ±1.0 for maximum harmonic energy
3. **Horner's method**: Efficient ~30 cycles/sample evaluation
4. **Feedback cap**: Zone 3 limited to 25% to prevent runaway
5. **Phase offsets**: Create orthogonal parameter sweeps
6. **CPU usage**: Moderate; polynomial evaluation is optimized
7. **Stereo width**: Zone 0 (Width) only audible in stereo monitoring
