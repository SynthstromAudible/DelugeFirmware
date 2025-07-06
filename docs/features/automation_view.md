# Description

Automation View is a new view that complements the existing Arranger and Clip Views.

- The Automation Arranger View is accessed from within Arranger View by pressing the Shift + Song buttons
  - Note: the automation arranger view editor for a specific parameter can be accessed directly from the arranger song menu for that parameter (e.g. the one you would access by pressing on select encoder or by using the shift + shortcut buttom combo) by pressing the Song button while in the parameter value.
- The Automation Clip View is accessed from within the Clip View by pressing the Clip button
  - Note: the automation clip view editor for a specific parameter can be accessed directly from the clip sound menu for that parameter (e.g. the one you would access by pressing on select encoder or by using the shift + shortcut buttom combo) by pressing the Clip button while in the parameter value.

It allows you to edit automatable parameters on a per step basis at any zoom level.

Automatable Parameters are broken down into four categories for Automation View purposes:

1. Automatable Clip View Parameters for Synths and Kits with a row selected and affect entire DISABLED

> The 81 parameters that can be edited are:
>
> - **Master** Level, Pitch, Pan
> - **LPF** Frequency, Resonance, Morph
> - **HPF** Frequency, Resonance, Morph
> - **EQ** Bass, Bass Frequency, Treble, Treble Frequency
> - **Reverb** Amount
> - **Delay** Rate, Amount
> - **Sidechain** Shape
> - **Distortion** Decimation, Bitcrush, Wavefolder
> - **OSC 1** Level, Pitch, Phase Width, Carrier Feedback, Wave Position
> - **OSC 2** Level, Pitch, Phase Width, Carrier Feedback, Wave Position
> - **FM Mod 1** Level, Pitch, Feedback
> - **FM Mod 2** Level, Pitch, Feedback
> - **Env 1** Attack, Decay, Sustain, Release
> - **Env 2** Attack, Decay, Sustain, Release
> - **Env 3** Attack, Decay, Sustain, Release
> - **Env 4** Attack, Decay, Sustain, Release
> - **LFO 1** Rate
> - **LFO 2** Rate
> - **LFO 3** Rate
> - **LFO 4** Rate
> - **Mod FX** Offset, Feedback, Depth, Rate
> - **Arp** Rate, Gate, Spread Gate, Spread Octave, Spread Velocity, Ratchet Amount, Ratchet Probability, Chord Polyphony, Chord Probability, Note Probability, Bass Probability, Swap Probability, Glide Probability, Reverse Probability, Rhythm, Sequence Length
> - **Noise** Level
> - **Portamento**
> - **Stutter** Rate
> - **Compressor** Threshold
> - **Mono Expression** X (Pitch Bend)
> - **Mono Expression** Y (Mod Wheel)
> - **Mono Expression** Z (Channel Pressure / Aftertouch)

> You can also edit any patch cables that are created

2. Automatable Clip View Parameters for Kits with affect entire ENABLED, and Audio Clips

> The 26 parameters that can be edited are:
>
> - **Master** Level, Pitch, Pan
> - **LPF** Frequency, Resonance, Morph
> - **HPF** Frequency, Resonance, Morph
> - **EQ** Bass, Bass Frequency, Treble, Treble Frequency
> - **Reverb** Amount
> - **Delay** Rate, Amount
> - **Sidechain** Level, Shape
> - **Distortion** Decimation, Bitcrush
> - **Mod FX** Offset, Feedback, Depth, Rate
> - **Stutter** Rate
> - **Compressor** Threshold

3. Automatable Parameters for Arranger

> The 22 parameters that can be edited are:
>
> - **Master** Level, Pan
> - **LPF** Frequency, Resonance, Morph
> - **HPF** Frequency, Resonance, Morph
> - **EQ** Bass, Bass Frequency, Treble, Treble Frequency
> - **Reverb** Amount
> - **Delay** Rate, Amount
> - **Distortion** Decimation, Bitcrush
> - **Mod FX** Offset, Feedback, Depth, Rate
> - **Stutter** Rate

4. Automatable Patch Cables and Modulation Depths

You can automate all patch cables and modulation depths.

Simply enter the modulation menu that displays `SOURCE -> DESTINATION` and then press `CLIP` to enter Automation View for that specific Patch Cable.

5. Automatable CC's for MIDI Clips

> You can automate MIDI CC's 0-119, along with Pitch Bend and After Touch. Note: Mod Wheel is MIDI CC 1.
> In Automation View for MIDI Clips, MIDI CC's have been mapped to grid shortcuts using the template file from MIDI Follow Mode (MIDIFollow.XML). So if you want to change what CC's map to what grid shortcuts in the Automation View for MIDI Clips, you would need to edit the MIDIFollow.XML template for MIDI Follow mode.
> See Appendix below for an overview of the MIDI CC to Grid Shortcut mappings

Automation View can be thought of as a layer sitting on top of the Arranger View and Clip View for Synths, Kits, MIDI, and Audio clip types.

For Arranger, the sidebar from Arranger View is available in the Automation Arranger View so that you can change clip statuses. If you are in Automation Overview it will also provide access to audition clips.

For Non-Audio Instrument Clip Types (Synths, Kits, and MIDI), the sidebar from these instrument clip types is available in the Automation Clip View so that you can audition sounds while viewing/editing parameter automations.

The Automation View functionality covered by this feature can be broken down into these sections:

1. Entering/Exiting the Automation View
2. Automation Overview
3. Parameter Selection
4. Automation Editor
5. Button Shortcuts/Combos
6. Default Settings (Enhancements to the existing Arranger / Clip View behaviours)

A more detailed break-down of the above sections is found below.

## Enter/Exit Automation View

The Automation Arranger View can be accessed from the Arranger View by pressing the Shift + Song buttons.

The Automation Clip View can be accessed from the Clip View by pressing on the Clip button. The Clip button will flash when you are in the Automation Clip View

Automation View can also be accessed from the Sound menu for any automatable parameter, patch cable, modulation depth. While in the menu, press Clip (if you're in a clip) or Song (if you're in arranger) to exit out of the menu and enter the Automation Editor for that parameter.

Some additional things to note:

- If you are in Automation Clip View and transition to Song/Arranger View, the Deluge will remember that on a per Clip basis. This means that when you transition back to a Clip it will re-load the Automation Clip View if you were in that view previously.

> **Note:** This information is saved with the song.

- For non-Audio Instrument Clip Types, you can load up the Keyboard Screen while in the Automation Clip View so you can quickly switch between editing a Parameter and playing notes on the Keyboard.
- Automation Clip View settings are saved per Instrument per Clip with the Song
- Automations that are recorded in using the Mod Encoders (Gold Knobs) are always recorded with Interpolation.
- MPE automations are not covered/edited by the Automation Clip View. If MPE must be edited/nudged/shifted, it must be done in the current Instrument Clip View.

## Automation Overview

The Automation Overview provides an overview of the Parameters that can be automated, are currently automated, and acts as a launchpad for the Automation Editor.

The Automation Overview **will:**

- Allow you to automate Master FX section parameters using the Mod Encoders (gold knobs)
- For Arranger View, Synth, Kit, and Audio clips, illuminate the shortcut pad's on the grid that correspond to a parameter that can be automated.
- For MIDI clips, illuminate the pads to represents the CC values that can be automated from 0-121 (with the last two being Pitch Bend and After Touch and are placed in the bottom right hand corner).
- provide visual feedback on which parameters that can be automated and are currently automated by illuminating the corresponding shortcut pads white

> **Note:** While in the automation overview, if you hit play, record and then record in Master FX automations using the Mod Encoder (Gold Knobs), the automation overview grid will update to show you that those parameters are now automated by highlighted the corresponding pads on the grid white. Note: the update will happen after you turn record off.

- be quickly accessible at any time from within the Automation Arranger View by pressing affect entire
- be quickly accessible at any time from within the Automation Clip View by holding shift + clip, audition + clip, or affect entire.

> **Note 1:** The Automation Overview will also be displayed if you change clip types (e.g. from Synth to MIDI).

> **Note 2:** In a Kit clip, the Automation Overview will be displayed any time you change your kit row selection (e.g. by pressing on the mute or audition pads).

> **Note 3:** In a Kit clip, if you press and hold an audition pad and turn the vertical encoder, you can quickly scroll through all automation overview's for kit clip's rows

- enable you to quickly access the Automation Editor for any automatable parameter by pressing any of the pads that are illuminated or by turning select
- enable you to clear all automations using the current combo of pressing down on Horizontal Encoder at the same time as pressing the Back button (note: for kit clip the behaviour will operate differently than usual: with affect entire enabled you will only clear all kit level automations. with affect entire disabled it will clear all kit row level automations).

> **Note:** When automations are cleared, Parameters are reset to the current value in the Sound Editor. E.g. if an automation playing back and you deleted it mid playback, the parameter value would be set to the last played back value. Or if you just edited the automation by pressing on the grid, the last value would be the value corresponding to the last pad you pressed.

- enable you to quickly turn interpolation on/off by using the shift + interpolation shortcut (the interpolation shortcut pad is the interpolation pad in the first column of the grid, second from the top). The interpolation shortcut pad will blink every few seconds to remind you that interpolation is on.
- enable you to copy paste automations and the overview grid will show you the movement of pasted automations by illuminating the pads of the params that automation was copied to white
- allow you to use the keyboard screen
- allow you to mute/audition sounds using the sidebar

The Automation Overview **will not allow you to:**

- access the sample browser
- use regular grid shortcuts (except for shift + param)
- view/change the clip length / row length settings (go into the automation editor or back to the regular instrument clip view if you want to view/edit those settings)
- edit existing note row values in a synth (e.g. change a pad from F to F#)
- edit existing note row sounds in a kit (e.g. change the sample)
- add new rows in a kit
- re-arrange rows in a kit
- enter the Automation Editor in Automation Arranger View if you are currently auditioning a clip

## Parameter Selection

You can select the Parameter that you want to edit in four ways:

1. From the Automation Overview by pressing any of the illuminated pads
2. By turning select
3. From the menu by selecting a parameter for editing and then pressing song (if you're in arranger) or clip (if you're in a clip)
4. By pressing shift + the shortcut pad corresponding to the parameter you want to edit

Once you select a Parameter in the Automation Clip View, it will be remembered and stored on a per clip basis unless you go back to the Automation Overview. This means that if you are editing a Parameter and go back to the regular Clip View, Song View or Arranger View, when you transition back to the Automation Clip View it will open the last Parameter that you were editing in the Automation Editor. Similarly if you were last on the Automation Overview it will remember that.

> **Note:** This information is saved with the song.

## Automation Editor

The Automation Editor enables you to view and edit parameter automation values.

- For Arranger, automations are recorded on a song arrangement basis

- For Synths, MIDI and Audio clips, automations are recorded/edited on a clip basis (e.g. not on a per note row basis).

- For Kit clips, automations can be recorded/edited on a clip basis and on a row basis (two layers of automation).

The Automation Editor **will:**

- show you visually whether automation is enabled on a parameter by dimming the pads when automation is off, and increasing the brightness when automation is on
- display on the screen what parameter you are currently editing and its automation status (for 7seg it will only display on the screen for MIDI clips)
- enable you to use either of the Mod Encoders (gold knobs) to quickly change the parameter value of the parameter in focus. The knobs automatically map to the selected parameter and you can use either knob (eliminating the guess work about which knob to turn).
- enable you to quickly change parameters in focus for editing by turning select or using shift + shortcut pad or going back to automation overview using affect entire or shift/audition pad + clip
- enable you to view the current parameter value setting for the parameters that are currently automatable.
- illuminate each pad row according to the current value within the range of 0-128. E.g. bottom pad = 0-16, then 17-32, 33-48, 49-64, 65-80, 81-96, 97-112, 113-128)
  > **Update** The values displayed in automation view have been updated to display the same value range displayed in the menu's for consistency across the Deluge UI. So instead of displaying 0 - 128, it now displays 0 - 50. Calculations in automation view are still being done based on the 0 - 128 range, but the display converts it to the 0 - 50 range.
- edit new or existing parameter automations on a per step basis, at any zoom level across the entire timeline. Each row in a step column corresponds to a range of values in the parameter value range (0-128) (see above). If you press the bottom row, the value will be set to 0. if you press the top row, the value will be set to 128. Pressing the rows in between increments/decrements the value by 18.29 (e.g. 0, 18, 37, 55, 73, 91, 110, 128).
  > **Update** The values displayed in automation view have been updated to display the same value range displayed in the menu's for consistency across the Deluge UI. So instead of displaying 0 - 128, it now displays 0 - 50. Calculations in automation view are still being done based on the 0 - 128 range, but the display converts it to the 0 - 50 range.

<img width="297" alt="Screenshot 2024-03-16 at 5 13 50 PM" src="https://github.com/seangoodvibes/DelugeFirmware/assets/138174805/3d5dded1-efc2-4cb6-ad07-df2942fdc66e">

> **Note** For patch cables / modulation depth, the grid value ranges for each pad have been adapted to accomodate the full -50 to +50 range.
>
> The bottom pad in the grid will set the value to -50 and the top pad in the grid will set the value to +50.
>
> You can set the value to 0 by pressing and holding the two middle pads.
>
> The LED indicators for the mod encoders have been updated to show the full -50 to +50 range as well.
>
> This diagram shows the updated value ranges for the pads when automating a patch cable / modulation depth.

<img width="293" alt="Screenshot 2024-03-16 at 5 07 36 PM" src="https://github.com/seangoodvibes/DelugeFirmware/assets/138174805/e6f853f7-56bc-4102-8cc3-9b5a86bd4bfb">

- enable you to press two pads in a single automation column to set the value to the middle point between those two pads
- enable you to enter long multi-step automations by pressing and holding one pad and then pressing a second pad

> **Note 1:** to enter long multi-step automations across multiple grid pages you will need to zoom out as both pads pressed must be visible on the grid). Values in between steps are linearly calculated based on the value corresponding to the pads pressed. For example: you could program a sweep up from value 0 to value 128 by pressing and holding on pad 0,0 and then pressing on pad 15,8).

> **Note 2:** with interpolation turned off, the values between pads will sound like they are changing in a step fashion. to smooth out these values, turn interpolation on before entering your long multi-step automation. By playing with the interpolating setting you could create hybrid parameter changes that sound smooth and stepped.

![IMG_5827](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/890970a0-2496-40e9-926c-9a9bd5697c0a)

- enable you to fine tune the parameter automation values set by pressing and holding on a pad (or multiple pads) in a grid column and turning either of the Mod Encoders (Gold Knobs) (the value will increase/decrease relative to the current parameter value corresponding to the pad(s) you are holding).

> **Note 1:** you can also fine tune your long multi-step automations with the Mod Encoders (Gold Knobs).
>
> You have the ability to fine tune a long press's start and end values which automatically adjusts the interpolation between those values (without needing to enter the pad selection mode).
>
> Simply enter a long press and while continuing to hold the first pad in the long press, turn the gold knobs to adjust the start and end values of the long press.
>
> When refining a long presses start/end values, each gold knob is used separately to adjust the start/end position values.
>
> The bottom left gold knob is used to adjust the start value.
>
> The top right gold knob is used to adjust the end value.
>
> In a long press, you will also note that the LED indicators change to show the current value of the start and end position. Once you've let go of the long press, the LED indicators reset back to the parameters overall current value.

> **Note 2:** the existing master FX section is disabled in the automation editor and can only be accessed if you go back to the Automation Overview, Arranger View, Clip View, or open the keyboard screen. When you turn either of Mod Encoders (Gold Knobs) it will display the parameter value between 0 and 128 on the screen of the current parameter being edited (not the parameter selected in the master FX section).

> **Note 3:** If the parameter selected is not currently automated, turning the Mod Encoders (Gold Knobs) will increase the value of every step in the automation grid in unison (e.g. same value for all steps).

- you can also fine tune automation values by entering **Pad Selection Mode**. You enter / exit this mode by pressing the pad selection mode shortcut (Shift + Waveform Pad in first column of grid / the very top left pad). The waveform shortcut pad will blink every few seconds to remind you that you are in pad selection mode.

![image](https://github.com/seangoodvibes/DelugeFirmware/assets/138174805/1e4b3653-c1a1-4d21-bfa0-826df378b063)

> In pad selection mode, you cannot edit the automation grid by pressing on the pads.
>
> With a single or multi pad (long) press you select the pad(s) to edit.
>
> A cursor is displayed on the grid to identify the single or multiple pads you've selected. Seeing the cursor means you are in pad selection mode.
>
> Once you've made your pad selection, you can use the gold knobs to tweak the value of the singular pad or the value of the start/end pads in a long press.
>
> When one pad is selected, one cursor is displayed on the grid and both led indicators show the value for that pad (cursor position).
>
> When two pads are selected, two cursors are displayed on the grid and the lower led indicator shows the value of the left pad (left cursor), and the upper led indicator shows the value of the right pad (right cursor).
>
> With this mode you can also press on each pad to see its current value without changing its value.
>
> **Note 1:** if you've entered a long press and wish to see the value of each column in the long press, you will need to press and hold any pad in the column you wish to query.
>
> **Note 2:** if you've entered a long press and wish to switch back to a single press, you can do so by quickly pressing a pad in any column (e.g. don't hold it).

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
- display the current parameter being edited on the grid by flashing the parameter shortcut pad white. you can also see the name of the parameter you are editing by holding shift + pressing the flashing shortcut pad to see the parameter name on the screen. Note: for 7seg, you will only cc the MIDI CC # on the screen, not the parameter names in a Synth or Kit as the names are too complicated to abbreviate for a 7seg display and they are not currently shown in the sound editor).
- enable you to quickly turn interpolation on/off by using the shift + interpolation shortcut (the interpolation shortcut pad is the interpolation pad in the first column of the grid, second from the top). The interpolation shortcut pad will blink every few seconds to remind you that interpolation is on.
- delete automation for the current parameter being edited using shift + press on either of the Mod Encoders (Gold Knobs). You can also delete automation for the current parameter being edited by pressing down on Horizontal Encoder at the same time as pressing the Back button.

> **Note:** When automation is deleted, the Parameter's value is reset to the current value in the Sound Editor. E.g. if an automation playing back and you deleted it mid playback, the parameter value would be set to the last played back value. Or if you just edited the automation by pressing on the grid, the last value would be the value corresponding to the last pad you pressed.

- extend/shorten the arranagement / clip length using the current combo (shift + turn horizontal encoder)

> **Note:** For Arranger View, Synth, MIDI, Kit Clips with Affect Entire enabled, and Audio Clips, the Automation Editor grid will display the shortened arranagement/clip size (e.g. you cannot edit automation past the arrangement/clip length boundary).

- in automation clip view, extend/shorten a note row length using the current combo (audition pad + turn horizontal encoder)

> **Note 1:** For Synth and MIDI clips, the automation editor will not display the shortened note row, but you will see that it has been shortened when playing the clip as the cursor will move independently of other rows).

> **Note 2:** For Kit Clips with Affect Entire disabled, the Automation Editor grid will display the shortened clip row size (e.g. you cannot edit automation past the clip row length boundary).

- in automation clip view, double clip length using the current combo (shift + press down on horizontal encoder)
- enable you to quickly turn interpolation on/off by using the shift + interpolation shortcut (the interpolation shortcut pad is the interpolation pad in the first column of the grid, second from the top). The interpolation shortcut pad will blink every few seconds to remind you that interpolation is on.
- in automation clip view, enable you to mute/audition sounds using the sidebar
- in automation arranger view, enable you to change clip statuses using the sidebar

> **Note: ** In a kit, whenever you select a different kit row (whether via muting or auditioning), the Automation Overview will be opened as you are changing the focus of the automation view).

The Automation Editor **will not allow you to:**

- access the sample browser
- use regular grid shortcuts (except for shift + param)
- edit MPE parameter values
- edit existing note row values in a synth (e.g. change a pad from F to F#)
- edit existing note row sounds in a kit (e.g. change the sample)
- add new rows in a kit
- re-arrange rows in a kit
- change root note in a synth/MIDI/CV clip
- audition clips in Automation Arranger View

## Default Settings

Described below are a couple of toggable enhancements that change existing behaviour in the Clip View but are of benefit to the user now that Automation's can be edited separately from the Arranger / Clip View.

These default settings are found in the `Settings` Menu under `Defaults/Automation/`.

### Interpolation

Interpolation does not change existing behaviour. This toggle is only used in the new Automation View.

This sets the default value for the Interpolation setting in the Automation View.

> **Note:** This default value is loaded every time you enter the Automation View.

Interpolation itself can be toggled on/off by using the shift + interpolation shortcut (the interpolation shortcut pad is the interpolation pad in the first column of the grid, second from the top). The interpolation shortcut pad will blink every few seconds to remind you that interpolation is on.

Interpolation is only applied when you are physically entering a long automation pad press (e.g. when you hold one pad and press a second pad to connect the pads together). If you turn interpolation on and do not edit any parameter values, it will not have any effect.

With interpolation turned on it will sound like values are smoothly transitioning from one value to the next.

With interpolation turned off it will sound like a value is jumping from one value to the next.

The default setting for Interpolation an be changed in the in the `Settings/Defaults/Automation/Interpolate` menu.

### Clearing Automation

Currently in the Arranger View and Instrument Clip View if you press down on the Horizontal Encoder and press the Back button at the same time, all **Clips/Notes** AND **Automations** are cleared.

This is not ideal as your goal may be to simply re arrange your clips / re record your notes without changing your parameter automations.

It is important to recognize that in the Deluge, Arranagement Clips / Clip Notes, are recorded **separately** from the Automations. That is to say that if you remove a clip from arranger or a note from an instrument clip, the automation will still exist, and if you clear an automation, the clip in arranger / note in an instrument clip will still exist.

Thus, this default setting clearly delineates this behaviour by changing the Arrangement/Clip Clearing behaviour in the Arranger View / Instrument Clip View to only clear Clips or Notes, and not Automations.

Similarly, in the Automation View, clearing the arranagement or clearing the clip will only clear Automations, and not the Arranagement clips or Clip Notes.

Through the `Settings/Defaults/Automation/Clear` submenu, you can disable this change in behaviour and re-instate the original automation clearing behaviour by setting 'Clear' to "Disabled."

> **Note:** for kit clips, clip clearing is delineated between clearing the kit level automations (when affect entire is on) and the kit row level automations (when affect entire is off).

> **Note:** MPE recorded will still be cleared as the scope of this PR does not cover editing MPE.

### Shifting Automation Horizontally

Currently in the Arranger View if you press down on Shift and turn the Horizontal Encoder, it will shift the arrangement of clips and ALL Automations of the entire arranagement horizontally.

Currently in the Instrument Clip View if you press down on the Vertical Encoder and turn the Horizontal Encoder or hold shift , it will shift Notes AND the ALL Automations of the entire clip horizontally.

This default setting will disable the shifting of Automations within the existing Arranger / Clip Views and leave that behaviour to the Automation View.

In the Automation View, functionality is provided to shift automations at the parameter level.

Through the `Settings/Defaults/Automation/Shift` submenu, you can disable this change in behaviour and re-instate the original automation shifting behaviour by setting 'Shift' to "Disabled."

> **Note:** MPE recorded will still be shifted as the scope of this PR does not cover editing MPE.

### Nudging Notes

Currently in the Instrument Clip View if you press down on a note and turn the Horizontal Encoder, it will nudge the Note AND the Automations for that same step.

This is not ideal as your goal may be to simply nudge the note and not the parameter automation.

This default setting addresses this by changing the behaviour to only nudge notes.

Through the `Settings/Defaults/Automation/Nudge Notes` submenu, you can disable this change in behaviour and re-instate the original nudge note behaviour by setting 'Nudge Notes' to "Disabled."

> **Note:** there is no Automation nudging functionality in the Automation Clip View. But you can shift the entire Automation sequence horizontally left/right or manually edit the automation.

> **Note:** MPE recorded will still be nudged as the scope of this PR does not cover editing MPE.

### Disable Audition Pad Shortcuts

Currently in the Instrument Clip View if you hold down an audition pad and press a shortcut pad on the grid, it will open the menu corresponding to that shortcut pad.

By default in the Automation Clip View that same behaviour of holding an audition pad and pressing a shortcut pad is disabled in favour of you being able to hold down an audition pad and adjust the automation lane values so that you can audible hear the changes to the sound while adjusting automation settings.

Through the `Settings/Defaults/Automation/Disable Audition Pad Shortcuts` submenu, you can disable this change in behaviour and re-instate the audition pad shortcuts by setting 'Disable Audition Pad Shortcuts' to "Disabled."

> **Note:** in automation view, shortcuts do not open the menu. They change the selected parameter for automation lane editing.

# Fun things to try with the new Automation Clip View

## Two Hand Automation Drumming

1. Set a long sustaining clip length note.

2. Press play.

3. Switch to Automation Clip View

4. Enter the Automation Editor for a Parameter, e.g. LPF Frequency is a good one

5. Now start drumming on the LPF Frequency Automation Lanes and see how much fun you can have adjusting automation with all your fingers at once

## Editing Automations with your left hand and playing the piano roll with your right hand

1. While in the Automation Clip View, start playing the Piano roll in the sidebar

2. While you're playing the Piano roll, use your left hand to select the parameters you want to edit. E.g. while holding an audition pad you can press the clip button to go back to the Automation Overview and select a Parameter.

3. Select a Parameter and enter the Automation Editor and while you're playing the Piano roll with your right hand, start adjusting the automation lanes with your left hand

4. Quickly switch back to the Automation Overview and select another Parameter

5. Keep the flow going! Playing the Piano roll and switching between all the Parameters at the same time to create some amazing rhythmic sounds

# Development Notes

This view is tightly integrated with the Instrument Clip View and calls many of the functions and writes to many of the variables in the Instrument Clip View.

Some functions and variables that were previously Private in the Instrument Clip View were made Public so that they could be used by the Automation Clip View.

The Automation Clip View is recognized as a new UI so many of the calls that check the current UI in view (e.g. getCurrentUI(), getRootUI()) were modified to check if the Automation Clip View is currently open.

Also, similar to the Keyboard screen which uses the variable "onKeyboardScreen" to tell other views (e.g. Song, Arranger) whether to open up the Keyboard screen (instead of the Instrument Clip View) when transitioning to/from the Instrument Clip View / Audio Clip View, the Automation Clip View also uses a similar variable "onAutomationClipView" for the same purpose.

# De-scoped Items (Future Release)

- Key Frames + Parameter value for each frame
- Automation Shapes
- Zoom in Vertically
- Shading of lower node changes when viewing higher node level
- How can we reset automation to default preset levels
- Can automation changes work as an offset to preset levels? (Eg if you increase preset default, it shifts all automation up, if you decrease preset default, it shifts all automation down
- Can you use the buttons in the FX bar to cycle through parameters in the automation editor
- Can automation run at a sequence length independent of clip/row? (Currently no, the deluge isn't built that way)
- MPE note automation view

# Button Shortcuts/Combos

These are the main button shortcuts/combos that will be used in the Automation Clip View

## Automation Overview

![Automation Overview Shortcuts](https://github.com/user-attachments/assets/a2a58c06-f094-49dc-913c-b32ac4137b34)

## Automation Editor

![Automation Editor Shortcuts](https://github.com/user-attachments/assets/eff8f5d7-46dd-46da-b114-72b79ffdb076)

## Additional Shortcut Combos for Automation Arranger View

- Hold `SHIFT` and press `SONG` to enter Automation Arranger View from Arranger View.
- Press `SONG` while in Automation Arranger View to exit back to Arranger View
- Press `AFFECT ENTIRE` while in Automation Arranger View to go back to Automation Overview
- Press `CROSS SCREEN` while in Automation Arranger View to activate/de-activate automatic scrolling during playback

## Additional Shortcut Combos for Entering the Automation Editor from the Sound Menu's

- While in the Arranger Song Menu for a specific parameter, press `SONG` to enter the Automation Editor for that parameter
- While in the Clip Sound Menu for a specific parameter, press `CLIP` to enter the Automation Editor for that parameter

## Automation Overview of Grid Shortcuts mappings to MIDI CC's for MIDI Clips

This image shows the default MIDI Follow CC to Grid Parameter Shortcut mappings. You can edit these mappings in MIDIFollow.XML which will update the CC to Grid Parameter Shortcut mappings used in Automation View for MIDI Clips.

![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/6ecbb774-deb9-466c-9469-e86e5e904ce3)
