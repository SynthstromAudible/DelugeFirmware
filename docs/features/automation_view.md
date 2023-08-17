# Description

Automation Instrument Clip View is a new view that complements the existing Instrument Clip View (which is accessed by pressing the Clip Button). It allows you to edit automatable parameters on a per step basis at any zoom level.

Automatable Parameters are broken down into two categories for Automation Instrument Clip View purposes:

1. Automatable Parameters for Synths, Kits with affect entire DISABLED, and Midi

>The 55 parameters that can be edited are:
>
> - **Master** Level, Pitch, Pan
> - **LPF** Frequency, Resonance, Morph
> - **HPF** Frequency, Resonance, Morph
> - **EQ** Bass, Bass Frequency, Treble, Treble Frequency
> - **Reverb** Amount
> - **Delay** Rate, Amount
> - **Sidechain** Level, Shape
> - **Distortion** Decimation, Bitcrush, Wavefolder
> - **OSC 1** Level, Pitch, Phase Width, Carrier Feedback, Wave Position
> - **OSC 2** Level, Pitch, Phase Width, Carrier Feedback, Wave Position
> - **FM Mod 1** Level, Pitch, Feedback
> - **FM Mod 2** Level, Pitch, Feedback
> - **Env 1** Attack, Decay, Sustain, Release
> - **Env 2** Attack, Decay, Sustain, Release
> - **LFO 1** Rate
> - **LFO 2** Rate
> - **Mod FX** Offset, Feedback, Depth, Rate
> - **Arp** Rate, Gate
> - **Noise** Level
> - **Portamento**

2. Automatable Parameters for Kits with affect entire ENABLED

>The 24 parameters that can be edited are:
>
> - **Master** Level, Pan, Pitch
> - **LPF** Cutoff, Resonance
> - **HPF** Cutoff, Resonance
> - **EQ** Bass, Bass Frequency, Treble, Treble Frequency
> - **Reverb** Amount
> - **Delay** Rate, Amount
> - **Sidechain** Level, Shape
> - **Distortion** Decimation, Bitcrush
> - **Mod FX** Offset, Feedback, Depth, Rate
> - **Arp** Gate
> - **Portamento**

It can be thought of as a layer sitting on top of the Instrument Clip View for Synths, Kits and Midi instrument clip types. This PR does not address automation for audio clips.

The sidebar from the instrument clip types is available in the Automation Instrument Clip View so that you can audition sounds while viewing/editing parameter automations.

The Automation Instrument Clip View functionality covered by this PR can be broken down into these sections:

1. Entering/Exiting the Automation Instrument Clip View
4. Automation Overview
5. Parameter Selection
6. Automation Editor
7. Button Shortcuts/Combos
8. Community Features (Enhancements to the existing Instrument Clip View)

A more detailed break-down of the above sections is found below.

## Enter/Exit Automation Instrument Clip View

The Automation Instrument Clip View can be accessed from the Instrument Clip View by pressing on the Clip button. The Clip button will flash when you are in the Automation Instrument Clip View

Some additional things to note:

- If you are in Automation Instrument Clip View and transition to Song/Arranger View, the Deluge will remember that on a per Instrument Clip basis. This means that when you transition back to an Instrument Clip it will re-load the Automation Instrument Clip View if you were in that view previously. 

> **Note:** This information is saved with the song.

- You can load up the Keyboard Screen while in the Automation Instrument Clip View so you can quickly switch between editing a Parameter and playing notes on the Keyboard.
- Automation settings are saved per Instrument per Clip with the Song
- Automations that are recorded in using the Mod Encoders (Gold Knobs) are always recorded with Interpolation.
- MPE automations are not covered/edited by this Automation Instrument Clip View. If MPE must be edited/nudged/shifted, it must be done in the current Instrument Clip View.

## Automation Overview

The Automation Overview provides an overview of the Parameters that can be automated, are currently automated, and acts as a launchpad for the Automation Editor.

The Automation Overview **will:**

- Allow you to automate Master FX section parameters using the Mod Encoders (gold knobs)
- For Synth and Kit clips, illuminate the shortcut pad's on the grid that correspond to a parameter that can be automated.
- For Midi clips, illuminate the pads to represents the CC values that can be automated from 0-121 (with the last two being Pitch Bend and After Touch and are placed in the bottom right hand corner).
- provide visual feedback on which parameters that can be automated and are currently automated by illuminating the corresponding shortcut pads white

> **Note:** While in the automation overview, if you hit play, record and then record in Master FX automations using the Mod Encoder (Gold Knobs), the automation overview grid will update to show you that those parameters are now automated by highlighted the corresponding pads on the grid white. Note: the update will happen after you turn record off.

- be quickly accessible at any time from within the Automation Instrument Clip View by holding shift + clip or by pressing the instrument clip type button that corresponds to the type of clip you are currently in (e.g. if you're in a Synth, press Synth). 

> **Note 1:** The Automation Overview will also be displayed if you change clip types (e.g. from Synth to Midi).

> **Note 2:** In a Kit clip, the Automation Overview will be displayed any time you change your kit row selection (e.g. by pressing on the mute or audition pads).

> **Note 3:** In a Kit clip, if you press and hold an audition pad and turn the vertical encoder, you can quickly scroll through all automation overview's for kit clip's rows

- enable you to quickly access the Automation Editor for any automatable parameter by pressing any of the pads that are illuminated or by turning select
- enable you to clear all automations using the current combo of pressing down on Horizontal Encoder at the same time as pressing the Back button (note: for kit clip the behaviour will operate differently than usual: with affect entire enabled you will only clear all kit level automations. with affect entire disabled it will clear all kit row level automations).

> **Note:** When automations are cleared, Parameters are reset to the current value in the Sound Editor. E.g. if an automation playing back and you deleted it mid playback, the parameter value would be set to the last played back value. Or if you just edited the automation by pressing on the grid, the last value would be the value corresponding to the last pad you pressed.

- enable you to quickly turn interpolation on/off by pressing down on the select encoder
- enable you to quickly access the Automation Community Features sub-menu by using holding shift and pressing down on the select encoder
- enable you to copy paste automations and the overview grid will show you the movement of pasted automations by illuminating the pads of the params that automation was copied to white
- allow you to use the keyboard screen
- allow you to mute/audition sounds using the sidebar

The Automation Overview **will not allow you to:**

- access the regular settings menu
- access the sound editor
- access the sample browser
- use regular grid shortcuts (except for shift + param)
- view/change the clip length / row length settings (go into the automation editor or back to the regular instrument clip view if you want to view/edit those settings)
- edit existing note row values in a synth (e.g. change a pad from F to F#)
- edit existing note row sounds in a kit (e.g. change the sample)
- add new rows in a kit
- re-arrange rows in a kit

## Parameter Selection

You can select the Parameter that you want to edit in three ways:

1. From the Automation Overview by pressing any of the illuminated pads
2. By turning select
5. By pressing shift + the shortcut pad corresponding to the parameter you want to edit
9. Once you select a Parameter, it will be remembered and stored on a per clip basis unless you go back to the Automation Overview. This means that if you are editing a Parameter and go back to the regular Instrument Clip View, Song View or Arranger View, when you transition back to the Automation Instrument Clip View it will open the last Parameter that you were editing in the Automation Editor. Similarly if you were last on the Automation Overview it will remember that. 

> **Note:** This information is saved with the song.

## Automation Editor

The Automation Editor enables you to view and edit parameter automation values.

- For Synths and Midi clips, automations are recorded/edited on a clip basis (e.g. not on a per note row basis).

- For Kit clips, automations can be recorded/edited on a clip basis and on a row basis (two layers of automation).

The Automation Editor **will:**

- show you visually whether automation is enabled on a parameter by dimming the pads when automation is off, and increasing the brightness when automation is on
- display on the screen what parameter you are currently editing and its automation status (for 7seg it will only display on the screen for Midi clips)
- enable you to use either of the Mod Encoders (gold knobs) to quickly change the parameter value of the parameter in focus. The knobs automatically map to the selected parameter and you can use either knob (eliminating the guess work about which knob to turn).
- enable you to quickly change parameters in focus for editing by turning select or using shift + shortcut pad
- enable you to view the current parameter value setting for the parameters that are currently automatable.
- illuminate each pad row according to the current value within the range of 0-128. E.g. bottom pad = 0-16, then 17-32, 33-48, 49-64, 65-80, 81-96, 97-112, 113-128) 
- edit new or existing parameter automations on a per step basis, at any zoom level across the entire timeline. Each row in a step column corresponds to a range of values in the parameter value range (0-128) (see above). If you press the bottom row, the value will be set to 0. if you press the top row, the value will be set to 128. Pressing the rows in between increments/decrements the value by 18 (e.g. 0, 18, 36, 54, 72, 90, 108, 128). 

> **Note 1:** as with the current instrument clip view, if you enter/modify automations at a lower zoom level (e.g. between steps), the higher zoom level will let you know you've done so by blurring the pad colour at the higher zoom level).

> **Note 2:** automation tails will be drawn based on the automation nodes created (eg tails are drawn between nodes). A node corresponds to a position in the sequencer timeline. Nodes for automation are created when you edit a parameter value at a specific step.

![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/56d76e68-12d8-4b2a-9100-aca615c63269)

- enable you to enter long multi-step automations by pressing and holding one pad and then pressing a second pad

> **Note 1:** to enter long multi-step automations across multiple grid pages you will need to zoom out as both pads pressed must be visible on the grid). Values in between steps are linearly calculated based on the value corresponding to the pads pressed. For example: you could program a sweep up from value 0 to value 128 by pressing and holding on pad 0,0 and then pressing on pad 15,8).

> **Note 2:** with interpolation turned off, the values between pads will sound like they are changing in a step fashion.  to smooth out these values, turn interpolation on before entering your long multi-step automation. By playing with the interpolating setting you could create hybrid parameter changes that sound smooth and stepped.

![IMG_5827](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/890970a0-2496-40e9-926c-9a9bd5697c0a)

- enable you to fine tune the parameter automation values set by pressing and holding on a pad (or multiple pads) in a grid column and turning either of the Mod Encoders (Gold Knobs) (the value will increase/decrease relative to the current parameter value corresponding to the pad(s) you are holding). 

> **Note 1:** the existing master FX section is disabled in the automation editor and can only be accessed if you go back to the Automation Overview, Instrument Clip View, or open the keyboard screen. When you turn either of Mod Encoders (Gold Knobs) it will display the parameter value between 0 and 128 on the screen of the current parameter being edited (not the parameter selected in the master FX section). Pressing on the Mod Encoders (Gold Knobs) still works and allows you to adjust different settings (such as reverb room size, LPF type, delay type, sidechain speed, etc.)

> **Note 2:** If the parameter selected in the master fx section matches the parameter that is currently being edited, then the parameter led value indicators will increase/decrease as you turn the Mod Encoders (Gold Knobs) or adjust the parameter on the grid. 

> **Note 3:** If the parameter selected is not currently automated, turning the Mod Encoders (Gold Knobs) will increase the value of every step in the automation grid in unison (e.g. same value for all steps).

- enable you to lay down longer automations with interpolation 

> **Note:** Interpolation only has an effect if you are entering a "long automation press." (see below). that is to say: you press and hold one pad and then press a second pad.

> **Note:** If an automation value has been entered with the interpolation setting turned on and then subsequently the interpolation setting is turned off, the automation value will not be affected unless you to re-enter it.

- enable you to lay down automations without interpolation (parameter values change in a step ladder fashion). 

> **Note:** If an automation value has been entered with the interpolation setting turned off and then subsequently the interpolation setting is turned on, the automation value will not be affected unless you to re-enter it.

- enable you to lay down automations by pressing two pads and linearly interpolate the values in between

> **Note:** With Interpolation On, the values will smoothly transition from the first pad pressed to the second pad pressed. With Interpolation Off, the values will transition in a step fashion from the first pad pressed to the second pad pressed. Note, as mentioned above, the interpolation setting only takes effect when you are editing the automation.

- enable you to shift the automation for the parameter in focus horizontally across the grid timeline by using the existing familiar combo of pressing down the vertical encoder and turning the horizontal encoder
- enable you to copy / paste automations from one parameter to another based on the parameter editor in view using the current combo (learn + press on either of the Mod Encoders (Gold Knobs) to copy and learn + shift + press on either of the Mod Encoders (Gold Knobs) to paste)
- enable you to record in automations live with either of the Mod Encoders (Gold Knobs) so that you can see your automations displayed on the grid
- display the current parameter being edited on the grid by flashing the parameter shortcut pad white. you can also see the name of the parameter you are editing by holding shift + pressing the flashing shortcut pad to see the parameter name on the screen. Note: for 7seg, you will only cc the Midi CC # on the screen, not the parameter names in a Synth or Kit as the names are too complicated to abbreviate for a 7seg display and they are not currently shown in the sound editor).
- quickly change the interpolation on/off setting by pressing down on the settings encoder
- delete automation for the current parameter being edited using shift + press on either of the Mod Encoders (Gold Knobs)

> **Note:** When automation is deleted, the Parameter's value is reset to the current value in the Sound Editor. E.g. if an automation playing back and you deleted it mid playback, the parameter value would be set to the last played back value. Or if you just edited the automation by pressing on the grid, the last value would be the value corresponding to the last pad you pressed.

- extend/shorten the clip length using the current combo (shift + turn horizontal encoder)

> **Note:** For Synth, Midi and Kit Clips with Affect Entire enabled, the Automation Editor grid will display the shortened clip size (e.g. you cannot edit automation past the clip length boundary).

- extend/shorten a note row length using the current combo (audition pad + turn horizontal encoder)

> **Note 1:** For Synth and Midi clips, the automation editor will not display the shortened note row, but you will see that it has been shortened when playing the clip as the cursor will move independently of other rows).

> **Note 2:** For Kit Clips with Affect Entire disabled, the Automation Editor grid will display the shortened clip row size (e.g. you cannot edit automation past the clip row length boundary).

- double clip length using the current combo (shift + press down on horizontal encoder)
- enable you to quickly turn interpolation on/off by pressing down on the select encoder
- enable you to quickly access the Automation Community Features sub-menu by using holding shift and pressing down on the select encoder
- enable you to mute/audition sounds using the sidebar

> **Note: ** In a kit, whenever you select a different kit row (whether via muting or auditioning), the Automation Overview will be opened as you are changing the focus of the automation view).

The Automation Editor **will not allow you to:**

- access the regular settings menu
- access the sound editor
- access the sample browser
- use regular grid shortcuts (except for shift + param)
- edit MPE parameter values
- edit existing note row values in a synth (e.g. change a pad from F to F#)
- edit existing note row sounds in a kit (e.g. change the sample)
- add new rows in a kit
- re-arrange rows in a kit
- change root note in a synth/midi/cv clip

## Community Features

Described below are a couple of toggable enhancements that change existing behaviour in the Instrument Clip View but are of benefit to the user now that Automation's can be edited separately from the Instrument Clip View.

### Interpolation

Interpolation does not change existing behaviour. This toggle is only used in the new Automation Instrument Clip View. 

This sets the default value for the Interpolation setting in the Automation Instrument Clip View. 

> **Note:** This default value is loaded every time you enter the Automation Instrument Clip View.

Interpolation itself can be toggled on/off by pressing down on the Setting button however that does not change the default setting.

Interpolation is only applied when you are physically entering a long automation pad press (e.g. when you hold one pad and press a second pad to connect the pads together). If you turn interpolation on and do not edit any parameter values, it will not have any effect.

With interpolation turned on it will sound like values are smoothly transitioning from one value to the next.

With interpolation turned off it will sound like a value is jumping from one value to the next.

### Clearing Clips

Currently in the Instrument Clip View if you press down on the Horizontal Encoder and press the Back button at the same time, all **Notes** AND **Automations** are cleared.

This is not ideal as your goal may be to simply re record your notes without changing your parameter automations.

It is important to recognize that in the Deluge, Notes, are recorded **separately** from the Automations. That is to say that if you remove a note, the automation will still exist, and if you clear an automation, the note will still exist.

Thus, this community feature clearly delineates this behaviour by changing the Clip Clearing behaviour in the Instrument Clip View to only clear Notes, and not Automations.

Similarly, in the Automation Instrument Clip View, clearing a clip will only clear Automations, and not Notes.

This change can be reversed by setting the "Clip Clear" community feature to OFF.

> **Note:** for kit clips, clip clearing is delineated between clearing the kit level automations (when affect entire is on) and the kit row level automations (when affect entire is off).

> **Note:** MPE recorded will still be cleared as the scope of this PR does not cover editing MPE.

### Nudging Notes

Currently in the Instrument Clip View if you press down on a note and turn the Horizontal Encoder, it will nudge the Note AND the Automations for that same step.

This is not ideal as your goal may be to simply nudge the note and not the parameter automation.

This community feature addresses this by changing the behaviour to only nudge notes.

This change can be reversed by setting the "Nudge Notes" community feature to OFF.

> **Note:** there is no Automation nudging functionality in the Automation Instrument Clip View. But you can shift the entire Automation sequence horizontally left/right or manually edit the automation.

> **Note:** MPE recorded will still be nudged as the scope of this PR does not cover editing MPE.

### Shifting Clip Horizontally

Currently in the Instrument Clip View if you press down on the Vertical Encoder and turn the Horizontal Encoder, it will shift Notes AND the ALL Automations of the entire clip horizontally.

Similar to above with the Nudging Notes community feature, this feature will disable the shifting of Automations within the existing Instrument Clip View and leave that behaviour to the Automation Instrument Clip View.

In the Automation Instrument Clip View, functionality is provided to shift automations at the parameter level.

> **Note:** MPE recorded will still be shifted as the scope of this PR does not cover editing MPE.

# Fun things to try with the new Automation Instrument Clip View

## Two Hand Automation Drumming

1. Set a long sustaining clip length note.

2. Press play.

5. Switch to Automation Instrument Clip View

6. Enter the Automation Editor for a Parameter, e.g. LPF Frequency is a good one

7. Now start drumming on the LPF Frequency Automation Lanes and see how much fun you can have adjusting automation with all your fingers at once

## Editing Automations with your left hand and playing the piano roll with your right hand

1. While in the Automation Instrument Clip View, start playing the Piano roll in the sidebar

2. While you're playing the Piano roll, use your left hand to select the parameters you want to edit. E.g. while holding an audition pad you can press the clip button to go back to the Automation Overview and select a Parameter.

4. Select a Parameter and enter the Automation Editor and while you're playing the Piano roll with your right hand, start adjusting the automation lanes with your left hand

5. Quickly switch back to the Automation Overview and select another Parameter

6. Keep the flow going! Playing the Piano roll and switching between all the Parameters at the same time to create some amazing rhythmic sounds

# Development Notes

This view is tightly integrated with the Instrument Clip View and calls many of the functions and writes to many of the variables in the Instrument Clip View.

Some functions and variables that were previously Private in the Instrument Clip View were made Public so that they could be used by the Automation Instrument Clip View.

The Automation Instrument Clip View is recognized as a new UI so many of the calls that check the current UI in view (e.g. getCurrentUI(), getRootUI()) were modified to check if the Automation Instrument Clip View is currently open.

Also, similar to the Keyboard screen which uses the variable "onKeyboardScreen" to tell other views (e.g. Song, Arranger) whether to open up the Keyboard screen (instead of the Instrument Clip View) when transitioning to/from the Instrument Clip View, the Automation Clip View also uses a similar variable "onAutomationClipView" for the same purpose.

# De-scoped Items (Future Release)

- Improving interpolation (I want to get more granular, do the calculation at the lowest possible node)
- Adjust multi pad press so that it renders the second pad's value at the last possible node (within that pad)
- Automation for Audio Clips
- Key Frames + Parameter value for each frame
- Automation Shapes
- Jump from Sound Editor
- Zoom in Vertically
- Shading of lower node changes when viewing higher node level
- How can we reset automation to default preset levels
- Can automation changes work as an offset to preset levels? (Eg if you increase preset default, it shifts all automation up, if you decrease preset default, it shifts all automation down
- Can you use the buttons in the FX bar to cycle through parameters in the automation editor
- Is automation in song/arranger views possible?
- Can automation run at a sequence length independent of clip/row? (Currently no, the deluge isn't built that way)
- MPE note automation view
- Cross-screen automation editing

# Button Shortcuts/Combos

These are the main button shortcuts/combos that will be used in the Automation Instrument Clip View

## Automation Overview

![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/108d68d6-b11e-431d-9910-454e1905eb7f)

## Automation Editor

![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/7da4e6f2-b8f0-4a07-9b06-6ad47b0419c2)
