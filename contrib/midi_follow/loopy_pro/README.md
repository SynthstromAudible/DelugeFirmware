# Loopy Pro Template for Deluge MIDI Follow Mode

This folder contains template files for [Loopy Pro](https://loopypro.com/). They provide an interactive touch surface to control the currently-active clip on the Deluge using [MIDI Follow Mode](https://delugecommunity.com/features/midi_follow_mode/).

## Template Design

- The control surface is located in page "B". Page "A" is just the default page with donuts to loop. You can delete
  page "A" if you don't need it.
- Page "C" contains the controls that are doing the heavy lifting of sending/receiving midi so it should NOT be deleted.
  That is, page "B" are the "user facing" controls, tied to the stepped dials from page "C".
- The controls are set up to send and receive on channel 15 when the Deluge is connected via USB (and detected as “Deluge Port
  1”), so you must go to your Deluge, and do Shift + Select to enter the main menu, go to MIDI -> MIDI-FOLLOW -> CHANNEL A,
  and set it to 15.

## Setup

#### Turn on MIDI Follow Feedback

In Loopy, go to Menu -> Control Settings -> MIDI Devices section -> Deluge Port 1 -> make sure
  that Feedback switch is enabled.
  In your Deluge, do Shift + Select to enter the main menu, go to MIDI -> MIDI-FOLLOW -> FEEDBACK. Here you can select the
  Channel to send feedback to, the Rate at which feedback is sent for Automation, and you must set Filter Responses to DISABLED.


#### Start a new project

Copy the Loopy Pro template from [c1.2](c1.2/) or [c1.3](c1.3/) to your Apple device and open the template in Loopy Pro.


#### Import the MIDI Follow template into an existing project
  - In case you already have a Loopy Pro project or template and you want to import the MIDI follow control surface into it, do
    the following:
    - Open the "Deluge Midi Follow"project and go to Settings -> Control Settings -> Current Project -> click "Midi Follow" profile.
    - On the top right click "TRANSFER" and copy it to Global Profiles.
    - Open your project and go to Settings -> Control Settings -> Global -> click "Midi Follow" profile.
    - On the top right click "TRANSFER" and copy it to Current Project. Ok, now the MIDI bindings are imported. Now we need to import
      the widgets.
    - Open again the "Deluge Midi Follow" project and click the pencil to edit the UI, and drag a rectangle selection all the page.
      Copy it, open your own project and in an empy page, paste it. Do the same with the other page with Stepped Dials.

### Troubleshooting

#### Error: "Couldn't import project: failed to open zip file"

If download the raw template file from GitHub on your Apple device gives you the error above, download the on a computer and transfer it to your device through iCloud or USB.
  
#### Deluge appears in Loopy Pro under a different name

In case your port is detected with a different name in Loopy (it could happen if the language of your iOS device is not English), like for example "Deluge **Puerto** 1" (in Spanish), you can always transfer the existing midi bindings from one port to the other by going to Loopy's Menu -> Control Settings -> Current Project -> Default -> look for the "Deluge Port 1" section and tap on "TRANSFER" to copy or move all the midi bindings to the real port name of your Deluge.


## Variants
### Template for Community Firmware 1.3
Author: Monitus

![c1.3 Template](c1.3/Deluge%20c1.3%20Loopy%20Pro%20Template.png)
The template in [c1.3](c1.3/) provides a broad set of controls and includes features up to [Deluge community firmware 1.3](https://delugecommunity.com/changelogs/changelog/#c130), such as ENV3+4 and LFO 3+4.

### Template for Community Firmware 1.2
Author: Dream Reactor

![c1.2 Template](c1.2/Deluge%20c1.2%20Loopy%20Pro%20Template.png)

Video demo: [Loopy Pro, MIDI Fighter Twister, and Synthstrom Deluge Control Setup](https://www.youtube.com/watch?v=O0l5VL7sZfM)

The template in [c1.2](c1.2/) is a branch of the [c1.3](c1.3/) template with a focused set of controls and includes features up to [Deluge community firmware 1.2 (Chopin)](https://delugecommunity.com/changelogs/changelog/#c120-chopin).

The "Groove" (left) side of the Loopy Pro template focuses on volume and timing controls.

The "Vibe" (right) side of the Loopy Pro template focuses on effect and filter controls.



#### Companion MIDI Fighter Twister template![c1.2 Template](c1.2/Deluge%20c1.2%20Loopy%20Pro%20MFT%20Companion%20Template.png)
The c1.2 Loopy Pro template includes a companion MIDI Fighter Twister (MFT) template, which allows Loopy Pro to provide visual feedback while the MFT provides tactile control. The MFT template communicates with Loopy Pro, which in turn communicates with the Deluge.

#### Control layout

- Bank 1 is mapped to the "Groove" side of the Loopy Pro template.

- Bank 2 is mapped to the "Vibe" side of the Loopy Pro template.

- Switch banks by tapping the Groove or Vibe buttons in Loopy Pro, or by pressing the top-right knob on the MFT.


