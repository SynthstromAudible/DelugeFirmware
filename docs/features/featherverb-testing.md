# Featherverb Reverb Testing Guide

## TL;DR

Featherverb is a lightweight 4-tap FDN/allpass cascade reverb optimized for the Deluge's memory constraints (77KB vs Mutable's 128KB). It offers 3 zone-based controls for matrix rotation, room size, and decay character.

**Quick Start:**
1. Load any synth or sample
2. FX menu → Reverb → Model → Featherverb
3. Turn **Matrix** (Zone 1) to change early reflection character
4. Turn **Size** (Zone 2) to control room size and topology
5. Turn **Decay** (Zone 3) to shape tail density and feedback

**Suggested Test Patch:**
- Use a percussive sound (plucks, keys, or drums) to hear reflections clearly
- Start with Size in middle zones (Lake/Trees) for natural room sound
- Higher Size zones (Owl/Vast) create extended ambient tails

## Step-by-Step Walkthrough

### 1. Basic Setup
1. Load a patch with clear transients (piano, plucks, or percussion)
2. Open FX menu → Reverb → select Featherverb model
3. Set reverb amount/send to ~30-50% to hear the effect clearly
4. Start with all zones near center for a balanced room sound

### 2. Explore Matrix (Zone 1)
Matrix controls the feedback rotation through orthogonal space, affecting early reflection character:

| Zone | Name | Character |
|------|------|-----------|
| 0 | L/R | Basic stereo separation |
| 1 | Front | Forward-focused reflections |
| 2 | Depth | Deeper room sense |
| 3 | Space | Expanded spatial image |
| 4 | Diffuse | Smeared, diffused reflections |
| 5 | Stereo | Enhanced stereo width |
| 6 | Swirl | Rotating spatial movement |
| 7 | Complex | Maximum matrix modulation |

Sweep slowly to hear how early reflections change character.

### 3. Explore Size (Zone 2)
Size controls room dimensions and cascade topology:

| Zone | Name | Topology | Character |
|------|------|----------|-----------|
| 0 | Mouse | Standard 2x | Tiny room, fast decay |
| 1 | Rabbit | Standard 2x | Small room |
| 2 | Lake | Standard 2x | Medium room |
| 3 | Trees | Standard 2x | Large natural space |
| 4 | Feather | Dual parallel 2x | Experimental texture |
| 5 | Sky | Nested 2x | Extended, responsive |
| 6 | Owl | Nested 4x | Wide stereo, smeared |
| 7 | Vast | Nested 4x | Maximum tail, self-limiting |

**Note:** Zones 6-7 use 4x undersampling for extended tails with lower CPU.

### 4. Explore Decay (Zone 3)
Decay shapes the tail character through feedback and density controls:

| Zone | Name | Character |
|------|------|-----------|
| 0 | Balanced | Even decay across spectrum |
| 1 | Attack | Emphasized transients |
| 2 | Sustain | Extended tail sustain |
| 3 | Bounce | Rhythmic reflection patterns |
| 4 | Scoop | Hollowed mid frequencies |
| 5 | Hump | Boosted mid frequencies |
| 6 | Sparse | Minimal density, distinct echoes |
| 7 | Dense | Maximum allpass density |

### 5. Explore Pre-delay
1. Navigate to Pre-delay parameter (0-50, maps to 0-100ms)
2. Increase for separation between dry and wet signals
3. **Push encoder** → toggles dry signal subtraction (removes bleedthrough)
4. Size (Zone 2) modulates tap spacing in multi-tap predelay

### 6. Combine Zones for Character
Try these combinations:
- **Tight room**: Mouse + Balanced + low Matrix
- **Concert hall**: Trees + Sustain + Space matrix
- **Ambient pad**: Vast + Dense + Complex matrix
- **Experimental**: Owl + Sparse + Swirl matrix

## Access Points

- **FX Menu** → Reverb → Model → Featherverb
- **Reverb submenu** contains Zone 1/2/3 and Pre-delay
- **Gold knob** adjusts selected parameter

## Parameters

### Zone Controls

| Parameter | Range | Description |
|-----------|-------|-------------|
| Matrix (Zone 1) | 0-50 (8 zones) | Feedback matrix rotation |
| Size (Zone 2) | 0-50 (8 zones) | Room size and topology |
| Decay (Zone 3) | 0-50 (8 zones) | Tail density and character |
| Pre-delay | 0-50 (0-100ms) | Delay before reverb onset |

### Internal Scaling

| Size Zone | Cascade Scale | Undersample |
|-----------|--------------|-------------|
| 0-3 | 1.0x-1.4x | 2x |
| 4 (Feather) | 1.4x | 2x |
| 5 (Sky) | 1.5x | 2x |
| 6 (Owl) | 1.6x | 4x |
| 7 (Vast) | 1.8x | 4x |

## Testing Scenarios

### 1. Basic Reverb
- [ ] Enable Featherverb → hear room ambience
- [ ] Increase reverb send → more wet signal
- [ ] Adjust Size → room grows/shrinks
- [ ] Adjust Decay → tail lengthens/shortens

### 2. Matrix Character
- [ ] Sweep Matrix slowly → hear reflection pattern changes
- [ ] L/R (zone 0) → simple stereo
- [ ] Complex (zone 7) → maximum spatial movement
- [ ] Matrix affects early reflections more than tail

### 3. Size Topology Changes
- [ ] Mouse-Trees (zones 0-3) → standard FDN behavior
- [ ] Feather (zone 4) → dual parallel cascades, textured
- [ ] Sky (zone 5) → nested topology, extended and responsive
- [ ] Owl/Vast (zones 6-7) → 4x undersample, maximum length

### 4. Decay Shaping
- [ ] Balanced → even natural decay
- [ ] Attack → punchy, transient-forward
- [ ] Dense → thick, lush tail
- [ ] Sparse → distinct, rhythmic reflections

### 5. Pre-delay Behavior
- [ ] Pre-delay 0 → reverb starts immediately
- [ ] Pre-delay 50 → 100ms gap before reverb
- [ ] Push encoder → toggle dry subtraction
- [ ] Size zone affects multi-tap spacing

### 6. Zone Combinations
- [ ] Small bright: Mouse + Attack + Front
- [ ] Large warm: Trees + Sustain + Depth
- [ ] Ambient wash: Vast + Dense + Complex
- [ ] Weird space: Owl + Sparse + Swirl

### 7. CPU and Memory
- [ ] Zones 0-5 use 2x undersample (lower CPU)
- [ ] Zones 6-7 use 4x undersample (lowest CPU, longest tail)
- [ ] Total buffer: ~77KB (efficient vs other models)
- [ ] Switching models may cause brief audio gap

### 8. Phi Triangle Evolution
- [ ] Matrix uses phi triangles for 3x3 feedback modulation
- [ ] Sweep Matrix → hear quasi-periodic rotation patterns
- [ ] Decay uses triangle waves for per-stage allpass coefficients
- [ ] No exact repetition across zone range

## Known Behaviors

1. **Topology modes**: Zones 4-7 use different internal routing (parallel, nested)
2. **4x undersample**: Owl/Vast zones have slightly darker character due to undersampling
3. **Self-limiting**: Vast zone has built-in feedback limiting to prevent runaway
4. **LFO modulation**: Sky zone has pitch modulation; Owl/Vast cap it to avoid artifacts
5. **Memory efficient**: 77KB total vs 128KB for Mutable reverb
6. **Dry subtraction**: Push pre-delay encoder to remove dry bleedthrough
