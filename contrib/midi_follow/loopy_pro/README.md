# Loopy Pro Template for Deluge MIDI Follow Mode

This folder contains template files for [Loopy Pro](https://loopypro.com/). They provide an interactive touch surface to control the currently-active clip on the Deluge using [MIDI Follow Mode](https://delugecommunity.com/features/midi_follow_mode/).


## Variants
### Template for Community Firmware 1.3
Author: Monitus

![c1.3 Template](c1.3/Deluge%20c1.3%20Loopy%20Pro%20Template.png)
The template in [c1.3](c1.3/) provides a broad set of controls and includes features up to [Deluge community firmware 1.3](https://delugecommunity.com/changelogs/changelog/#c130), such as ENV3+4 and LFO 3+4.

### Template for Community Firmware 1.2
Author: Dream Reactor

![c1.2 Template](c1.2/Deluge%20c1.2%20Loopy%20Pro%20Template.png)
The template in [c1.2](c1.2/) is a branch of the [c1.3](c1.3/) template with a focused set of controls and includes features up to [Deluge community firmware 1.2 (Chopin)](https://delugecommunity.com/changelogs/changelog/#c120-chopin).

The "Groove" (left) side of the Loopy Pro template focuses on volume and timing controls.

The "Vibe" (right) side of the Loopy Pro template focuses on effect and filter controls.

#### Companion MIDI Fighter Twister template![c1.2 Template](c1.2/Deluge%20c1.2%20Loopy%20Pro%20MFT%20Companion%20Template.png)
The c1.2 Loopy Pro template includes a companion MIDI Fighter Twister (MFT) template, which allows Loopy Pro to provide visual feedback while the MFT provides tactile control. The MFT template communicates with Loopy Pro, which in turn communicates with the Deluge.

#### Control layout

- Bank 1 is mapped to the "Groove" side of the Loopy Pro template.

- Bank 2 is mapped to the "Vibe" side of the Loopy Pro template.

- Switch banks by tapping the Groove or Vibe buttons in Loopy Pro, or by pressing the top-right knob on the MFT.


## Template Design Notes
The templates use Stepped Dial components on a secondary page to maintain the state of the controls on the main page when re-opening a saved project.

When you use the main page controls, they interact with the Stepped Dials. The Stepped Dials contain the MIDI CC mappings and communicate with the Deluge.

