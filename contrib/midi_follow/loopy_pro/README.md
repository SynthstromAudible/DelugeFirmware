# Loopy Pro Template for Deluge MIDI Follow Mode

This folder contains template files for [Loopy Pro](https://loopypro.com/). They provide an interactive touch surface to control the currently-active clip on the Deluge using [MIDI Follow Mode](https://delugecommunity.com/features/midi_follow_mode/).

## Template Design


### Pages
Each template includes two main pages:

- One page contains the interactive controls that let you play the Deluge. You can customize this page by removing, resizing, or reorganizing the elements.
- Another page contains the functional elements. Stepped Dials on this page are synced with the interactive controls and communicate with the Deluge via MIDI. They restore the state of the controls when you save and load your Loopy Pro project. Don't delete this page or any of its elements.

### MIDI communication

This template works with the Deluge connected via USB and communicating over MIDI channel 15. It supports two-way control feedback between the Deluge and Loopy Pro.

## Setup

### Set up your Deluge

Set your Deluge to use MIDI channel 15 and enable feedback for MIDI Follow Mode.

1. On the Deluge, open the settings by pressing Shift+Select.
1. Go to **MIDI** > **MIDI-Follow** and do the following:
    1. In **Channel** > **Channel A**, set the value to 15.
    1. In **Feedback** > **Channel**, select **Channel A**.
    1. In **Feedback** > **Automation Feedback**, select **High**.
    1. In **Feedback** turn off **Filter Responses**.
1. Hold Back to exit settings.

### Set up Loopy Pro

1. In Loopy Pro, go to **Settings (≡)** > **Control Settings**.
1. In the **MIDI Devices** section, tap **Deluge Port 1**.
1. Check that **Feedback Enabled** is turned on.

**Note:** If your Deluge doesn't appear as "Deluge Port 1", see the Troubleshooting section on this page.

### Get the Loopy Pro template

On your Apple device, go to [c1.2](c1.2/) or [c1.3](c1.3/), tap the `.lpproj` file, then tap **Raw** to download the template.

### Start a project

Do one of the following.

#### Use the template to create a new project

 In the **Files** app, find and tap the template file to open it in Loopy Pro.

#### Import the template into an existing project
If you have an existing Loopy Pro project and you want to import the MIDI follow control surface into it, do the following:

#### Import the MIDI bindings
1. In the **Files** app, find and tap the template file to open it in Loopy Pro.
1. Go to **Settings (≡)** > **Control Settings**.
1. In the **Current Project** section, tap the **MIDI Follow** profile.
1. Tap **Transfer**, then tap **Global Profiles**.
1. Open your existing project and go to **Settings (≡)** > **Control Settings**.
1. In the **Global Profiles** section, tap the **MIDI Follow** profile.
Tap **Transfer**, then tap **Current Project**.

#### Import the widgets
1. Open the Deluge MIDI Follow template again.
1. Turn on the UI editor by tapping Edit (pencil icon).
1. On the page with the coloured interactive controls, drag a rectangle to select the whole page.
1. Press and hold an element, then tap **Copy**.
1. Open your existing project, add an empty page.
1. On the empty page, hold, then tap **Paste**.
1. Repeat these steps 1-6 to copy the page with Stepped Dials from the template to another page in your project.

### Troubleshooting

#### Error: "Couldn't import project: failed to open zip file"

If your Apple device shows the error above when you try to open the template file, download the `.lpproj` file on a computer and transfer it to your device through iCloud or USB.
  
#### Deluge appears in Loopy Pro under a different name

In case your port is detected with a different name in Loopy (it could happen if the language of your iOS device is not English), like for example "Deluge **Puerto** 1" (in Spanish), you can always transfer the existing midi bindings from one port to the other by going to Loopy's Menu -> Control Settings -> Current Project -> Default -> look for the "Deluge Port 1" section and tap on "TRANSFER" to copy or move all the midi bindings to the real port name of your Deluge.


## Variants
### Template for Community Firmware 1.3
Author: Monitus

![c1.3 Template](c1.3/Deluge%20c1.3%20Loopy%20Pro%20Template.png)
The template in [c1.3](c1.3/) provides a broad set of controls and includes features up to [Deluge community firmware 1.3](https://delugecommunity.com/changelogs/changelog/#c130), such as ENV3+4 and LFO 3+4.

#### Pages
- Page **A** is a generic page with loop elements. You can delete page **A** if you don't need it.
- Page **B** contains the interactive controls.
- Page **C** contains the functional elements.

### Template for Community Firmware 1.2
Author: Dream Reactor

![c1.2 Template](c1.2/Deluge%20c1.2%20Loopy%20Pro%20Template.png)

Video demo: [Loopy Pro, MIDI Fighter Twister, and Synthstrom Deluge Control Setup](https://www.youtube.com/watch?v=O0l5VL7sZfM)

The template in [c1.2](c1.2/) is a branch of the [c1.3](c1.3/) template with a focused set of controls and includes features up to [Deluge community firmware 1.2 (Chopin)](https://delugecommunity.com/changelogs/changelog/#c120-chopin).

#### Pages
- Page **D** contains the interactive controls.
- Page **.** contains the functional elements.

#### Interactive control layout
The "Groove" (left) side of the Loopy Pro template focuses on volume and timing controls.

The "Vibe" (right) side of the Loopy Pro template focuses on effect and filter controls.


#### Companion MIDI Fighter Twister template

![c1.2 Template](c1.2/Deluge%20c1.2%20Loopy%20Pro%20MFT%20Companion%20Template.png)

The c1.2 Loopy Pro template includes a companion MIDI Fighter Twister (MFT) template, which allows Loopy Pro to provide visual feedback while the MFT provides tactile control. The MFT template communicates with Loopy Pro, which in turn communicates with the Deluge.

#### Control layout

- Bank 1 is mapped to the "Groove" side of the Loopy Pro template.

- Bank 2 is mapped to the "Vibe" side of the Loopy Pro template.

- Switch banks by tapping the Groove or Vibe buttons in Loopy Pro, or by pressing the top-right knob on the MFT.


