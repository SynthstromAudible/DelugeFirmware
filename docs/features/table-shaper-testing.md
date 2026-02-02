# Table Shaper (PELLET) Testing Guide

## TL;DR

Table Shaper is an XY wavetable-based waveshaper that blends 6 basis functions (inflator, polynomial, hard clip, Chebyshev fold, sine fold, rectifier) through a combinatoric sweep. It creates many character combinations via phi-triangle interference patterns.

**Quick Start:**
1. Load a synth with dynamic content (keys, pads, or drums) (Limited support for square wave content as this is primarily a waveshaping device)
2. FX menu → Distortion → Table Shaper
3. Turn **Shape X** (Knee) to control saturation intensity
4. Turn **Shape Y** (Color) to sweep through tonal characters
5. Zone 6 on Y gives classic Oxford-style inflator

**Suggested Test Patch:**
- Use a patch with varied a consistent carrier to hear pure waveshaping harmonics (sine, triangle NOT square)
- Start with Shape X around 50%-75% for audible effect you may need to tune drive based on your input
- Sweep Shape Y slowly to hear the combinatoric evolution

## Step-by-Step Walkthrough

### 1. Basic Setup
1. Load a test patch--sine recommended to hear pure tone of the waveshaper
2. Open FX menu → shaping → select Table Shaper
3. Set **Shape X** to ~50% (controls drive/knee)
4. Set **Mix** to 100% to hear the effect clearly
5. Slowly sweep **Shape Y** to explore characters

### 2. Understand Shape X (Knee)
Shape X controls the saturation curve softness:
- **0%** → Linear bypass (tables deallocated, no processing)
- **25%** → Gentle saturation, soft knee
- **50%** → Moderate saturation
- **75%** → Aggressive saturation
- **100%** → Maximum saturation, hard knee

### 3. Explore Shape Y (Color)
Shape Y sweeps through 6 blended basis functions:

| Basis | Character |
|-------|-----------|
| Inflator | Expands quiet, compresses loud (punchy) |
| Polynomial | Soft tanh-like saturation |
| Hard Knee | Crisp aggressive clipping |
| Chebyshev T5 | Wavefold, synthy character |
| Sine Folder | Harmonic-rich folding |
| Rectifier | Diode-like asymmetric clipping |

Each basis oscillates at different phi-power frequencies, creating non-repeating combinations as Y sweeps.

### 4. Zone 6 Special: Oxford Inflator
At Shape Y ≈ 75% (zone 6), the effect becomes a pure Oxford-style inflator:
- Expands quiet signals
- Compresses loud signals
- Adds punch and presence without harsh clipping
- Only active when gammaPhase = 0 (no secret offset)

### 5. Explore Extreme Settings
- **X=0, Y=any** → bypass (linear)
- **X=100, Y=low** → soft, warm saturation
- **X=100, Y=mid** → aggressive clipping
- **X=100, Y=high** → wavefold/rectifier chaos

### 6. Use Mix for Parallel Processing
1. Set X and Y for desired character
2. Lower Mix to blend with dry signal
3. Parallel saturation often sounds more natural
4. Mix at 30-50% gives subtle enhancement

## Access Points

- **FX Menu** → Distortion → Table Shaper
- **Parameters**: Shape X, Shape Y, Mix, Drive
- **Mod Matrix** (synth only): Press select for modulation routing

## Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Shape X | 0-127 | Knee/drive intensity (0=bypass) |
| Shape Y | 0-1023 | Color sweep through basis blends |
| Mix | 0-50 | Wet/dry blend |
| Drive | patched | Per-sample input gain |

## Testing Scenarios

### 1. Basic Saturation
- [ ] Enable Table Shaper → hear added warmth/grit
- [ ] Increase Shape X → more saturation
- [ ] Shape X = 0 → complete bypass (tables freed)
- [ ] Mix blend → parallel saturation

### 2. Color Sweep (Shape Y)
- [ ] Low Y → soft polynomial/inflator character
- [ ] Mid Y → hard knee, aggressive clipping
- [ ] High Y → wavefold and rectifier artifacts
- [ ] Each Y position is unique (phi-triangle interference)

### 3. Oxford Inflator Mode
- [ ] Set Y to zone 6 (~75%) → inflator character
- [ ] Quiet signals expanded, loud compressed
- [ ] Adds punch without harsh distortion
- [ ] Only works when gamma secret = 0

### 4. Basis Function Sweep
- [ ] Slowly sweep Y with fixed X → hear each basis fade in/out
- [ ] Notice ~50% of range each basis is OFF (duty cycle)
- [ ] Creates zones where only 2-3 bases are active
- [ ] No two positions sound exactly alike

### 5. Drive Staging
- [ ] Low X + low Drive → subtle coloring
- [ ] High X + high Drive → extreme saturation
- [ ] Drive boosts input before table lookup
- [ ] Watch output levels (can clip)

### 6. Mix Blending
- [ ] Mix 100% → full effect
- [ ] Mix 50% → effect only applies to the peaks of the waveform
- [ ] Mix 0% → bypass
- [ ] Low Mix + high X → subtle warmth

### 7. Modulation (Synth Only)
- [ ] Press select on Drive/Mix → assign mod source
- [ ] LFO to Shape Y → animated timbral sweep
- [ ] Envelope to X → dynamic saturation
- [ ] Patched params show dot indicator

### 8. Double-Buffer Table Updates
- Triggered by Knee/Color changes
- [ ] Some clicks/cpu spikes may occur when regenerating tables (only when moving zone params)
- [ ] Audio thread never sees incomplete table

## Secret Menus

Push+twist on Shape X encoder reveals advanced controls:

- **Gamma Phase** → shifts all phi-triangle patterns
  - Creates completely different basis combinations
  - Display shows "G:N"
  - Disables zone 6 inflator mode when > 0

- **Extras Mask** (5-bit bitmask) → enables special effects:
  - Bit 0: Subharmonic gain modulation
  - Bit 1: Feedback comb filter
  - Bit 2: Bit rotation (aliasing artifacts)
  - Bit 3: LPF pre-transform (trying to get the effect to only target low harmonics)
  - Bit 4: ZC-reset integrator


## Phi Triangle Details

The 6 basis functions use irrational phi-power frequencies:
- Inflator: φ^2.25 (≈2.96)
- Polynomial: φ^2.0 (≈2.62)
- Hard Knee: φ^1.75 (≈2.32)
- Chebyshev: φ^2.5 (≈3.33)
- Sine Fold: φ^1.5 (≈2.06)
- Rectifier: φ^1.25 (≈1.83)

With 50% duty cycles and accelerating frequency (freqMult = 1 + Y² × 3), the bases create sparse gaps where only subsets are active.

## Known Behaviors

1. **Table deallocation**: X=0 frees memory (true bypass)
2. **1024-entry tables**: Linear interpolation between entries
3. **Double-buffering**: Prevents audio glitches during updates (partial success)
4. **Zone 6 inflator**: Only at gammaPhase=0, Y≈75%
5. **Phi interference**: many distinct character zones
6. **Extras effects**: Require secret menu on push twist X/Knee menu knob to enable
7. **Memory**: 2KB per table (2 tables for double-buffer)
8. **CPU**: Table lookup is fast; generation is slower (done on UI thread)
