# Arp Control Keyboard Layout User Guide

## Overview
The Arp Control keyboard layout provides a comprehensive interface for controlling arpeggiator parameters directly from the keyboard view.

## Accessing the Layout
1. Enter **Keyboard Mode** by pressing the **KEYBOARD** button
2. Navigate to the **Arp Control** layout using the **SELECT** encoder
3. The layout will display with the following pad arrangement:

## Pad Layout

### Row 0 (Top Row)
- **Pads 0-2**: Arp Mode (Green) - Cycle through arpeggiator presets
- **Pads 4-11**: Octaves (Blue) - Set number of octaves for arpeggiation
- **Pad 15**: Randomizer Lock (Yellow) - Toggle randomizer lock on/off

### Row 1
- **Pads 0-7**: Velocity Spread (Cyan) - Control velocity randomization
- **Pads 8-15**: Random Octave (Light Blue) - Control octave randomization

### Row 2
- **Pads 0-7**: Gate (Green) - Control note gate length
- **Pads 8-15**: Random Gate (Lime) - Control gate randomization

### Row 3
- **Pads 0-7**: Sequence Length (Orange) - Control arpeggiator sequence length
- **Pads 8-13**: Rhythm Patterns (White) - Visualize rhythm patterns
- **Pads 14-15**: Transpose (Red/Purple) - Transpose keyboard up/down

### Rows 4-7
- **Keyboard**: Standard keyboard layout for playing notes

## Controls

### Pad Controls
- **Touch any pad** to set the corresponding parameter value
- **Last touched pad** will be highlighted brightly
- **Other pads** in the same group will be dimmed for visual feedback

### Encoder Controls
- **Vertical Encoder**: Scroll through rhythm patterns (1-50)
- **Vertical Encoder Press**: Toggle rhythm pattern on/off
- **Horizontal Encoder**: Cycle Arp Sync Rates 16th, 8th notes etc.

### Parameter Values

#### Velocity Spread (Row 1, Pads 0-7)
- **Pad 0**: OFF (0)
- **Pad 1**: 10
- **Pad 2**: 20
- **Pad 3**: 25
- **Pad 4**: 35
- **Pad 5**: 40
- **Pad 6**: 45
- **Pad 7**: 50

#### Random Octave (Row 1, Pads 8-15)
- **Pad 8**: OFF (0)
- **Pad 9**: 5
- **Pad 10**: 10
- **Pad 11**: 15
- **Pad 12**: 20
- **Pad 13**: 25
- **Pad 14**: 30
- **Pad 15**: 35

#### Gate (Row 2, Pads 0-7)
- **Pad 0**: 1
- **Pad 1**: 10
- **Pad 2**: 20
- **Pad 3**: 25
- **Pad 4**: 35
- **Pad 5**: 40
- **Pad 6**: 45
- **Pad 7**: 50

#### Random Gate (Row 2, Pads 8-15)
- **Pad 8**: OFF (0)
- **Pad 9**: 5
- **Pad 10**: 10
- **Pad 11**: 15
- **Pad 12**: 20
- **Pad 13**: 25
- **Pad 14**: 30
- **Pad 15**: 35

#### Sequence Length (Row 3, Pads 0-7)
- **Pad 0**: OFF (0)
- **Pad 1**: 10
- **Pad 2**: 20
- **Pad 3**: 25
- **Pad 4**: 35
- **Pad 5**: 40
- **Pad 6**: 45
- **Pad 7**: 50

## Rhythm Patterns
- **50 rhythm patterns** available (1-50)
- **Pattern 0**: OFF (no rhythm)
- **Visual feedback** on pads 8-13 shows the current pattern
- **Bright white**: Active steps in current pattern
- **Dim white**: Inactive steps in current pattern
- **Scroll** with vertical encoder to browse patterns
- **Press** vertical encoder to toggle pattern on/off

## Arpeggiator Presets
- **OFF**: Arpeggiator disabled
- **UP**: Ascending arpeggio
- **DOWN**: Descending arpeggio
- **BOTH**: Up and down arpeggio
- **RANDOM**: Random note order
- **WALK**: Walking arpeggio pattern
- **CUSTOM**: User-defined pattern

## Display Information
- **OLED Display**: Shows current parameter values and rhythm pattern names
- **"OFF" Display**: Shown for zero values instead of "0"
- **Parameter Names**: Displayed with current values (e.g., "Velocity: 25", "Seq Length: OFF")

## Track Type Compatibility
- **Synth Tracks**: Full functionality with all parameters
- **MIDI Tracks**: Full functionality with proper parameter scaling
- **CV Tracks**: Full functionality with proper parameter scaling

## Tips for Use
1. **Start with basic settings**: Set gate, velocity, and sequence length first
2. **Use rhythm patterns**: Experiment with different patterns for varied arpeggiation
3. **Randomization**: Use random octave and gate for more organic patterns
4. **Visual feedback**: Watch the pad colors to understand current settings
5. **Preset cycling**: Use horizontal encoder to quickly try different arpeggiator modes

## Troubleshooting
- **No sound**: Check that arpeggiator is not set to OFF
- **Parameters not changing**: Ensure you're on a compatible track type (Synth, MIDI, or CV)
- **Rhythm not working**: Make sure sync rate is set appropriately for rhythm patterns
- **Display issues**: Check that OLED display is functioning properly

## Version Information
- **Version**: BETA 0.2
- **Compatibility**: Deluge Community Firmware v1.3.0-dev
- **Last Updated**: 2025

---

*This guide covers the Arp Control keyboard layout for the Deluge generative sequencer. For additional help, refer to the main Deluge manual or community documentation.*
