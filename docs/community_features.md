# Community Features

## 1. Introduction

Every time a Pull Request improves the community firmware it shall be noted down what it accomplishes and how it is
used.

Please note that this document describes the state of the very latest
development work for a future Deluge version at a given time, and many of the
features are not yet available in released stable versions. Documentation
about released versions can be found here:

- [1.2.x (Chopin)](https://github.com/SynthstromAudible/DelugeFirmware/blob/release/1.2/docs/community_features.md)
- [1.1.x (Beethoven)](https://github.com/SynthstromAudible/DelugeFirmware/blob/release/1.1/docs/community_features.md)
- [1.0.x (Amadeus)](https://github.com/SynthstromAudible/DelugeFirmware/blob/release/1.0/docs/community_features.md)

For more detailed version information, see the [changelog](../CHANGELOG.md).

Reference the 'Community Features Menu' section at the end of this document to understand what each entry is and their
7SEG abbreviations.

## 2. File Compatibility Warning

In general, we try to maintain file compatibility with the official firmware. However, **files (including songs,
presets, etc.) that use community features may not ever load correctly on the official firmware again**. Make sure to
back up your SD card!

## 3. General Improvements

Here is a list of general improvements that have been made, ordered from newest to oldest:

#### 3.1 - Patch Cable Modulation Resolution

- ([#17]) Increase the resolution of "patch cables" between mod sources and destinations. This adds two decimal points
  when modulating parameters.
    - This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 3.2 - MPE

- ([#512]) Change handling of MPE expression when collapsed to a single MIDI channel. Previously Y axis would still be
  sent as CC74 on single MIDI channels. This changes it to send CC1 instead, allowing for controllable behaviour on more
  non-MPE synths. Future work will make a menu to set this per device.

- ([#934]) Allow converting polyphonic expression to monophonic expression. Two settings are added to midi clips under a
  new menu entry POLY EXPRESSION TO MONO. AFTERTOUCH converts MPE or poly aftertouch to channel aftertouch, and MPE
  converts poly y-axis to mod wheel and poly pitch bend to an average monophonic pitch bend. For y axis and aftertouch
  the highest value wins.

- ([#2343]) Allow converting output Y axis to mod wheel (cc1) to support synths with a limited MPE implementation, such
as the micromonsta and the dreadbox nymphes.

#### 3.3 - MIDI

- ([#47]) `Extra MIDI ports on the USB interface for MPE.` Port 2 shows in the MIDI device menu, and improves the
  usability of MPE-capable devices through the USB interface by allowing MPE zones to be sent to port 2 and non-MPE to
  be sent to port 1 (or vice versa). A third port is added for future use such as a desktop/mobile companion app, DAW
  control or Mackie HUI emulation. When USB for MIDI is plugged into the Deluge, you can browse these settings
  in `SETTINGS > MIDI > DEVICES > UPSTREAM USB PORT 1` or `UPSTREAM USB PORT 2`.

- ([#147]) Allows CCs to be learnt to the global commands (play, stop, loop, fill, etc.)

- ([#170]) A new `TAKEOVER` submenu was created in the `MIDI` settings menu which consists of four modes that can be
  selected from. This mode affects how the Deluge handles MIDI input for learned CC controls:

  **1. `JUMP`:** This is the default mode for the Deluge. As soon as a MIDI encoder/Fader position is changed, the
  Deluge's internal encoder position/Parameter value jumps to the position of the MIDI encoder/Fader.

  **2. `PICKUP`:** The Deluge will ignore changes to its internal encoder position/Parameter value until the MIDI
  encoder/Fader's position is equal to the Deluge encoder position. After which the MIDI encoder/Fader will move in sync
  with the Deluge.
    - Note: this mode will behave like the `JUMP` mode when you are recording or step editing automation.

  **3. `SCALE`:** The Deluge will increase/decrease its internal encoder position/Parameter value relative to the change
  of the MIDI encoder/Fader position and the amount of "runway" remaining on the MIDI controller. Once the MIDI
  controller reaches its maximum or minimum position, the MIDI encoder/Fader will move in sync with the Deluge. The
  Deluge value will always decrease/increase in the same direction as the MIDI controller.
    - Note: this mode will behave like the `JUMP` mode when you are recording or step editing automation.

  **4. `RELATIVE`:** The Deluge will increase/decrease its internal encoder position/Parameter value using the relative value changes (offset) sent by the controller. The controller must be actually sending relative value changes (127 for down and 1 for up) in order for this to work.

- ([#837]) `MIDI control over transpose / scale.`
    - Accessed via external MIDI via a new learnable global MIDI command in `SETTINGS > MIDI > CMD > TRANSPOSE`. It
      learns the entire channel, not just a single note.
    - Accessed internally from a MIDI clip. Turn the channel to the end of the list past the MPE zones to 'Transpose'.
      Notes in this clip now alter the transposition of the song.
    - Clips not in scale mode are unaffected (similar to the existing transpose behaviour from the encoders).
    - Configureable in `SETTINGS > MIDI > TRANSPOSE` between chromatic and in-scale transposition.
        - When set to `in-scale mode`:
            - an incoming MIDI note that is already in-scale will set the root note, and rotate the existing notes into
              place. Eg, starting in C major, an incoming D will set the root note to D, but keep the scale notes,
              resulting in D Dorian. For other scales, eg Harmonic minor, this rotation still applies.
            - Out of scale notes are ignored.
            - MIDI clips routed to transpose can be in either scale mode or chromatic. Using the audition pads, placing
              notes on the grid, or notes triggered during playback will cause the root note to move around on the
              audition pads, or in keyboard view.
        - When set to `chromatic mode`:
            - incoming notes set the root note. Scale mode clips are all transposed chromatically.
            - MIDI clips routed to 'Transpose' cannot be in scale mode. Existing clips of this type will drop back to
              chromatic mode, and pressing the Scale button will display 'CANT'.
    - Choice of octave is determined by the first note received by each song (this is reset on each song load). The
      first transpostino will move the root note by at most a fifth, to the closest matching note. Subsequent transpose
      events will respect the octave as normal.
        - Eg. Song is in C major. Transpose receives a D3. Regardless of whether the song is just a bassline, or pads,
          or a high synth part, it all goes up to D Dorian or D Major depending on the inkey/chromatic setting. After
          that a D4 will put everything up an octave. If instead the first transpose is a D4, this will initially only
          go up a tone, and after that a D5 will go up an octave, or a D3 will go down etc.
    - **Limitation** Just as with setting transposition from the encoders, a new transpose event will cut off currently
      playing notes. If this is done from a MIDI clip, it can cut off notes right at the start so they are never heard.
      Clip playback ensures transpose clips play first to affect new notes starting at the same position correctly,
      but any already sounding notes will be stopped.

- ([#889]) `Master MIDI Follow Mode` whereby after setting a master MIDI follow channel for Synth/MIDI/CV clips, Kit
  clips, and for Parameters, all MIDI (notes + cc’s) received will be directed to control the active or selected clip).
    - For a detailed description of this feature, please refer to the feature
      documentation: [MIDI Follow Mode Documentation]
    - Comes with a MIDI feedback mode to send updated parameter values on the MIDI follow channel for learned MIDI cc's.
      Feedback is sent whenever you change context on the deluge and whenever parameter values for the active context
      are changed.
    - Settings related to MIDI Follow Mode can be found in `SETTINGS > MIDI > MIDI-FOLLOW`.
    - ([#976]) For users of Loopy Pro, you will find a MIDI Follow template in this
      folder: [MIDI Follow Mode Loopy Pro Template]
        - It is setup to send and receive on channel 15 when the Deluge is connected via USB (and detected as “Deluge Port
          1”), so you must go to your Deluge, and do Shift + Select to enter the main menu, go to MIDI -> MIDI-FOLLOW -> CHANNEL A,
          and set it to 15. In case your port is detected with a different name in Loopy (it could happen if the language of your
          iOS device is not English), like for example "Deluge **Puerto** 1" (in Spanish), you can always transfer the existing
          midi bindings from one port to the other by going to Loopy's Menu -> Control Settings -> Current Project -> Default ->
          look for the "Deluge Port 1" section and tap on "TRANSFER" to copy or move all the midi bindings to the real
          port name of your Deluge.
        - As a bonus, this project also contains a page "B" with controls for the Song's global parameters, which must be learned
          individually (not part of MIDI Follow). To do that, go to Deluge Song view, click Select button to enter the
          Song menu, go to each parameter (Volume, Pan, LPF Freq, etc), hold Learn button and then move the knobs and
          faders within page "B" of Loopy's project.
        - Pages "C" and "D" are the controls that are doing the heavy lifting of sending/receiving midi so they can't be deleted.
          Pages "A" and "B" are just the "user facing" controls, tied to the stepped dials from the other two pages.
        - **How to setup Feedback:** In Loopy, go to Menu -> Control Settings -> MIDI Devices section -> Deluge Port 1 -> make sure
          that Feedback switch is enabled.
          In your Deluge, do Shift + Select to enter the main menu, go to MIDI -> MIDI-FOLLOW -> FEEDBACK. Here you can select the
          Channel to send feedback to, the Rate at which feedback is sent for Automation, and you must set Filter Responses to DISABLED.
    - ([#1053]) For users of Touch OSC, you will find a MIDI Follow template in this
      folder: [MIDI Follow Mode Touch OSC Template]

- ([#963]) `MIDI Select Kit Row` - Added new Select Kit Row setting to the MIDI Defaults menu, which can be found
  in `SETTINGS > MIDI > SELECT KIT ROW`. When this setting is enabled, midi notes received for learned kit row's will
  update the kit row selection in the learned kit clip. This also works with midi follow. This is useful because by
  updating the kit row selection, you can now control the parameters for that kit row. With midi follow and midi
  feedback enabled, this will also send updated cc feedback for the new kit row selection.

- ([#1251]) `MIDI learn for kit` - from song or arranger view, a kit can now be learnt to a midi channel in the same way
  as synths. The note sent for the learn will be treated as the first row for the kit, and increasing notes get mapped
  to the next rows

#### 3.4 - Tempo & Swing

- ([#178]) New option (`FINE TEMPO` in the `COMMUNITY FEATURES` menu). Inverts the push+turn behavior of the `TEMPO`
  encoder. With this option enabled the tempo changes by 1 when unpushed and ~4 when pushed (vs ~4 unpushed and 1 pushed
  in the official firmware).
    - This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

- ([#2264]) Swing interval can now be changed by holding `TAP TEMPO` button while turning the `TEMPO` encoder. To view current
  swing interval hold `TAP TEMPO` and press `TEMPO`. The menu for setting the swing interval has been moved from
  `SETTINGS > SWING INTERVAL` to `SONG > SWING INTERVAL`. Additionally, when changing either swing amount or interval using the
  `TEMPO` encoder, first encoder detent shows the current swing without changing it, with subsequent ones editing it.

- ([#2367]) Tempo changes can now be recorded to automation in arranger view

#### 3.5 - Alternative Delay Types for Param Encoders (Gold encoders)
- ([#282]) Ability to select in `COMMUNITY FEATURES` menu, which parameters are controlled when you click the `DELAY`
  -related golden encoders. The default (for upper and lower encoders) is `PINGPONG` (`ON/OFF`)
  and `TYPE` (`DIGITAL`/`ANALOG`), and you can modify it so the encoder clicks change
  the `SYNC TYPE` (`EVEN, TRIPLETS, DOTTED`) and `SYNC RATE` (`OFF, WHOLE, 2ND, 4TH, ETC`) respectively.
    - This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 3.6 - Kits

- ([#395]) Load synth presets into kit rows by holding the row's `AUDITION` + `SYNTH`. Saving can be done by holding the
  audition pad and pressing save.

#### 3.7 - Global Interface

- ([#118]) Sticky Shift - When enabled, tapping `SHIFT` will lock shift `ON` unless another button is also pressed
  during the short press duration.
    - This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.
- ([#118]) Shift LED feedback can now be toggled manually, however it will turn ON or OFF in conjunction with Sticky
  Shift (adjust SHIFT LED FEEDBACK afterwards).
    - This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.
- ([#1305]) Number of count-in bars is now configurable under `SETTINGS > RECORDING > COUNT-IN BARS`.

#### 3.8 - Mod Wheel

- ([#512]) Incoming mod wheel MIDI data from non-MPE devices now maps to the `Y` axis.

#### 3.9 - Visual Feedback on Value Changes with Mod Encoders and Increased Resolution for Value's in Menu's

- ([#636]) Changing parameter values with Mod (Gold) Encoders now displays a pop-up with the current value of the
  parameter. The `SOUND` and `MODULATION` screens when parameter and modulation editing have also been adjusted to show
  the same value range as displayed with the Mod Encoders.
    - This allows for better fine-tuning of values.
    - The value range displayed is 0-50 for non-MIDI parameters and 0-127 for MIDI parameters.
    - Note: In the Menu, if you wish to scroll through the parameter value range faster at an accelerated rate of +/- 5,
      hold `SHIFT` while turning the Select Encoder.

#### 3.10 - Enable Stutter Rate Automation

- ([#653]) Enabled ability to record stutter rate automation with mod (gold) encoder.
    - Note: This feature does not enable you to automate the triggering of stutter

#### 3.11 - Adjust Metronome Volume

- ([#683]) The Metronome's volume now respects the song's volume and will increase and decrease in volume together with
  the Gold Volume Encoder.
    - In addition, a `DEFAULTS` menu entry was created titled `METRONOME` which enables you to set a value between 1-50
      to further adjust the volume of the Metronome. 1 being the lowest metronome volume that can be heard when the
      Song's volume is at its maximum and 50 being the loudest metronome volume.

#### 3.12 - Mod Button Pop-up

- ([#888]) Added Mod Button pop-up to display the current Mod (Gold) Encoder context (e.g. LPF/HPF Mode, Delay Mode and
  Type, Reverb Room Size, Compressor Mode, ModFX Type and Param).

#### 3.13 - Automatically load the last open/saved song or user-defined template at startup.

- ([#1272]) Added feature to load automatically projects at startup.
  To activate the feature, press `SHIFT` + `SELECT` : `MENU > DEFAULTS > STARTUP SONG`.
- Four modes are available :
    - `NEW SONG` : default mode, empty project on clip view with default synth.
    - `TEMPLATE` : it will load `DEFAULT.XML` project that will be saved automatically when you select this mode, and
      will enable you to edit it later on and to replace the default blank song by a user-defined template.
    - `LAST OPENED SONG` : it will load the last long that you `OPENED` before shutting down the Deluge.
    - `LAST SAVED SONG` : it will load the least songs that you `SAVED` before shutting down the Deluge.
- In case of issue loading a song, a failsafe mode will be triggered :
    - If a crash occurs at startup loading a song, it will automatically deactivate the feature and will show you at startup which song is having issues.
    - the feature will be deactivated until either:
      - the canary file `SONGS/_STARTUP_OFF_CHECK{SONG}` is removed from UI song browser or from the sd card.
      - `LAST OPENED SONG`/ `LAST SAVED SONG` / `TEMPLATE` refers to another song, in which case previous canary file will be ignored (but still needs to be manually removed).
- In case of the crash still persists, you can always factory reset by holding `SELECT` while powering on the Deluge, which will force to boot up on `NEW SONG`.

#### 3.14 - Preserve Pad Brightness Preference Through Power Cycle

- ([#1312]) Save user-defined pad brightness level and restore it at startup.
  To use this feature, press `SHIFT` + `SELECT` : `MENU > DEFAULTS > PAD BRIGHTNESS`.
    - Default : Full brightness (`100`).
    - Min value `4`, Max Value `100`.

#### 3.15 - Learn Mod (Gold) Encoders to full Mod Matrix / Patch Cable value range
- ([#1382]) Mod (Gold) Encoders learned to the Mod Matrix / Patch Cable parameters can now access the full value range of those parameters (e.g. from -50 to +50)
  - In addition, a pop-up was added when using gold encoders learned to the Mod Matrix / Patch Cable parameters to show the source parameter(s) and destination parameter.

#### 3.16 - Make Mod (Gold) Encoders LED indicators bipolar
- ([#1480]) As a follow-up to [#1382] which enabled Gold Knobs to access the full range of a Patch cable, the LED indicators for gold knobs editing bipolar params (e.g. Pan, Pitch, Patch Cable), are now bipolar. The lights of a bipolar LED indicator are lit up as follows
  - Middle value = No lights lit
  - Maximum value = Top two lights fully lit
    - Between middle and maximum the top two lights will be lit up proportionately to the value in that range
  - Minimum value = Bottom two lights fully lit
    - Between middle and minimum, the bottom two lights will be lit up proportionately to the value in that range

#### 3.17 - Select Audio Clip Source from Audio Clip Menu
- ([#1531]) Added ability to select audio source from within an Audio Clip by opening the Audio Clip Sound Menu (`SHIFT` + `SELECT`) and Selecting the `AUDIO SOURCE` menu
  - Not included in c1.1.0
- ([#2371]) Source can now also be set to a specific track on the deluge. This enables an additional TRACK menu to choose
which track to record from. To run the instrument through the audio clip's FX choose the FX PROCESSING option

#### 3.18 - Set Audio Clip Length Equal to Sample Length
- ([#1542]) Added new shortcut to set the length of an audio clip to the same length as its sample at the current tempo. This functionally removes timestretching until the Audio Clip length or Song tempo is changed.
  - Press `▼︎▲︎` + `◀︎▶︎` to set the Audio Clip length equal to the length of the audio sample.
    - This action is also available in the `Audio Clip Sound Menu` (Press `SELECT`) by Selecting the `ACTIONS` menu and Pressing `SELECT` on the `Set Clip Length to Sample Length` action.
  - Press `SHIFT` + `◀︎▶︎` + `turn ◀︎▶︎` to adjust the audio clip's length independent of timestretching.

#### 3.19 - Sample Slice Default Mode

- ([#1589]) Added a new default setting that controls which playback mode new slices of a kit will get.
-  To change the setting, press `SHIFT` + `SELECT` : `MENU > DEFAULTS > SAMPLE SLICE MODE`.
- every new slice in a kit using the slicer will now get one of the modes by default
  -  `CUT`, `ONCE`, `LOOP`, `STRETCH`
  - the setting is persistent after reboot
  - if a kit slice is shorter then 2s, the slicer will automatically switch to `ONCE` (default behaviour)

#### 3.20 Default Hold Press Time

- ([#1846]) Added new default menu to set the length of time to register a `Hold Press` for use with `Sticky Shift`, `Performance View`, and the `Keyboard Sidebar Layouts.`
  - Set the default Hold Press time by accessing `SETTINGS > DEFAULTS > HOLD PRESS TIME`

#### 3.21 Eased Timeline Zoom Level Restrictions

- ([#1962]) The maximum zoom level has been increased. Now, the maximum zoom is the point the point where the entire timeline is represented by a single grid cell.
- This allows for more flexibility when entering long notes and chord progressions.
- While changing the zoom level, the horizontal encoder will briefly pause while passing the zoom level which represents the entire sequence. This is to prevent frustration from users who are used to the prior limitations.

#### 3.22 Exit menus by holding back

- ([#2166]) Holding back will now fully exit your current menu

#### 3.23 Automated Audio Exporting

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature documentation: [Audio Export Documentation]
- ([#2260]) Added `AUDIO EXPORT`, an automated process for exporting `CLIP's` while in `SONG VIEW` and `TRACK's` while in `ARRANGER VIEW`. Press `SAVE + RECORD` to start exporting audio. Press `BACK` to cancel the export and stop recording and playback.
- ([#2327]) You can also start the export via a new `EXPORT AUDIO` menu found in the `SONG` menu accessible in Song and Arranger Views. Start the export by entering the `SONG\EXPORT AUDIO\` menu and pressing `SELECT` on the menu item titled `START EXPORT`. It will exit out of the menu and display the export progress on the display.
- ([#2330]) You can configure settings for the audio export via the `EXPORT AUDIO` menu found in the `SONG` menu accessible in Song and Arranger Views. Enter `SONG\EXPORT EXPORT\CONFIGURE EXPORT\` to configure various export settings.

#### 3.24 Render Clip / Section Launch Event Playhead in Song Grid and Performance Views
- ([#2315]) A white playhead is now rendered in Song Grid and Performance Views that let's you know when a clip or section launch event is scheduled to occur. The playhead only renders the last 16 notes before a launch event.
  - Note: this playhead can be turned off in the Community Features submenu titled: `Enable Launch Event Playhead (PLAY)`

#### 3.25 Display Number of Bars / Notes Remaining until Clip / Section Launch Event
- ([#2315]) The display now shows the number of Bars (or Quarter Notes for the last bar) remaining until a clip or section launch event in all Song views (Grid, Row, Performance).

#### 3.26 Updated UI for Interacting with Toggle Menu's and Sub Menu's
- ([#2345]) For toggle (ON/OFF) menu's, you can now view and toggle the ON/OFF status without entering the menu by simply pressing on the `SELECT` encoder while the menu is selected.
 - OLED renders a checkbox that shows current ON/OFF status. Selecting that menu with select encoder will toggle the checkbox as opposed to entering the menu.
 - 7SEG renders a dot at the end of the menu item to show current ON/OFF status. Selecting that menu with select encoder will toggle the dot as opposed to entering the menu.
 - Submenu's on OLED are rendered with a ">" at the end to indicate that it is a submenu.

#### 3.27 Updated UI for Creating New Clips in New Tracks in Song Grid View
- ([#2429]) Added new mechanism for creating New Clips in New Tracks in `SONG GRID VIEW`
  - When you press a pad in a new track, a menu will appear asking you to confirm the type of clip you wish to create. The clip type selected to be created is shown on the display and is also indicated by the clip type button that is blinking.
    - The default clip type for new clips created can be configured in `SETTINGS > DEFAULTS > UI > CLIP TYPE > NEW CLIP TYPE` menu.
    - You can also configure whether the clip type for the next clip type you create should default to the last clip type you created. This helps with fast creation of multiple clips of the same type. You can enable this default setting in the `SETTINGS > DEFAULTS > UI > CLIP TYPE > USE LAST CLIP TYPE` menu.
    - If you just a tap a pad quickly to create a new clip, it will create that new clip using either the default clip type or the last clip type you created (if you enable this).
    - If you press and hold a pad, you can choose a different type to create in a number of ways:
      - by turning the select encoder to switch between the various clip types. You can create that clip type by pressing on the select encoder or letting go of the pad.
      - by pressing one of the clip type buttons (e.g. `SYNTH`, `KIT`, `MIDI`, `CV`).
      - If you let go of the pad without selecting a different type, it will create the clip using the last create type (or the last selected type if you changed selection using select encoder).
    - If you press `BACK` before releasing a pad or selecting a clip type, it will cancel the clip creation.
- These changes only apply to `SONG GRID VIEW` and NOT `SONG ROW VIEW`

#### 3.28 Add Clip Settings Menu in Song View to set Clip Mode and Clip Name and convert Instrument Clips to Audio Clips
- ([#2299]) Holding a clip in `SONG GRID VIEW` or the status pad for a clip in `SONG ROW VIEW` and pressing `SELECT` brings up a `CLIP SETTINGS` menu.
- If you open the menu with with an `INSTRUMENT CLIP` selected, then the menu will give you three options:
  1) `Convert to Audio`: Press select on this option to convert the selected `instrument clip` into an `audio clip`. The menu will exit after converting the clip.
    - Note: for `SONG ROW VIEW`, you can still convert an empty instrument clip to an audio clip the regular way by holding a pad for that clip in the main grid and pressing select.
  2) `Clip Mode`: Press select on this option to enter the `Clip Mode` menu so you can change the Clip Mode between `INFINITE`, `FILL` and `ONCE`.
  3) `Clip Name`: Press select on this option to enter the `Clip Name` UI to set the name for the clip.
- If you open the menu with an `AUDIO CLIP` selected, then the menu will give two options: `Clip Mode` and `Clip Name`.

#### 3.29 Polyphony / Voice Count
- ([#1824]) Added new `Max Voices (VCNT)` menu which lets you configure the Maximum number of Voices for a Polyphonic instrument, accessible by pressing `SELECT` in a `Synth clip` or `Kit clip with a Sound Drum selected and Affect Entire Off` under the `VOICE (VOIC)` menu.
  - This menu is also accessible from the `VOICE (VOIC) > Polyphony Type (POLY)` type menu by selecting `Polyphonic` and pressing `SELECT`
- Updated default `Max Voices` for new synth's to `8 voices`. Old synths for which a max number of voices has not been configured will default to `16 voices`.

#### 3.29 UI Accessibility Defaults Menu
- ([#2537]) Added `DEFAULTS (DEFA) > UI > ACCESSIBILITY (ACCE)` menu which contains accessibility changes to the Deluge UI to make the deluge more accessible to users with disabilities. These changes included:
  - `Shortcuts (SHOR)` to make specific shortcut combinations more accessible for users with mobility restrictions.
    - `HORIZONTAL ENCODER ◀︎▶︎` + `PLAY` is changed to `CROSS SCREEN` + `PLAY`
  - `Menu Highlighting (HIGH)` changes how menu highlighting is rendered on `OLED` displays by drawing a vertical bar `|` on the left edge of the display beside the selected menu item instead of highlighting the area of the selected menu item by inverting the text.

### 3.30 Added ability to Start / Restart Playback from Specific Clip Pad in Arranger View
- ([#2615]) Added ability to start / restart arrangement playback from the clip pad you're holding in arranger view.
  - Note: you need to select a pad of any clip in arranger in order for this to work (it cannot be an empty pad)

#### 3.31 Added Song New Midi Learn Menu
- ([#2645]) Added new `MIDI LEARN` menu to the `SONG` menu. In `Song Grid View` this menu enables you to learn `Clip/Section Launch`. In `Song Row View` this menu enables you to learn the `Clip/Section Launch` and `Instrument`.
  - While in this menu, you just need to `hold a clip / section` and send midi to learn that clip / section. If you press the `clip / section` again you will unlearn it.

## 4. New Features Added

Here is a list of features that have been added to the firmware as a list, grouped by category:

### 4.1 - Song View Features

#### 4.1.1 - Compressors

- ([#630]) In Song view, select `AFFECT ENTIRE` and the `SIDECHAIN`-related parameter button. Adjust the `UPPER` gold
  encoder for a single knob compressor with auto makeup gain (`ONE` mode). For detailed editing, press the `SIDECHAIN`
  -related gold encoder (`FULL` mode). The top LED will become a compression meter. Clicking the `REVERB`-related lower
  gold encoder will cycle through additional params: `RATIO` (displays ratio), `ATTACK` & `RELEASE` (shown in
  milliseconds) and Sidechain `HPF` (shown in Hz). The sidechain HPF is useful to remove some bass from the compressor
  level detection, which sounds like an increase in bass allowed through the compression. There is also a blend control
  to allow parallel compression.

    - `ATTACK`: 0ms - 63ms

    - `RELEASE`: 50ms - 363ms

    - `HPF SHELF`: 0hz - 102hz

    - `RATIO`: 2:1 - 256:1

    - `THRESHOLD`: 0 - 50

    - `BLEND` : 0-100%
- ([#1173]) In clip view, the settings are available under the COMPRESSOR menu entry. The same parameters exist there.
  In kits there is both a per row compressor, accessed through the menu when affect entire is off, and a kit compressor
  accessed while it is on.

#### 4.1.2 - Change Row Colour

- ([#163]) In Song View, pressing a clip row pad + `SHIFT` + turning `▼︎▲︎` changes the selected row color. This is
  similar to the shortcut when setting the color while in a clip view.

#### 4.1.3 - Fill Clips and Once Clips

- ([#196] and [#1018]) Holding a clip in `SONG GRID VIEW` or the status pad for a clip in `SONG ROW VIEW` and pressing `SELECT` brings up a `CLIP SETTINGS` menu. In this menu, you will find a submenu for `CLIP MODE`.

  The `CLIP MODE` menu enables you the set the following launch style options for a clip:
    - **`INFINITE (INF)`** - the default Deluge launch style.
    - **`Fill (FILL)`** - Fill clip.
        - When launched it will schedule itself to start at such a time that it _finishes_ at the start of the next
        loop. If the fill clip is longer than the remaining time, it is triggered immediately at a point midway through.
        The loop length is set by the longest playing clip, or by the total length of a section times the repeat count set for that section.
        - **Limitation**: a fill clip is still subject to the one clip per instrument behavior of the Deluge. Fill clips
          can steal an output from another fill, but they cannot steal from a non-fill. This can lead to some fills
          never starting since a default type clip has the needed instrument. This can be worked around by cloning the
          instrument to an independent copy.
    - **`Once (ONCE)`** - Once clip.
        - When triggered it will schedule itself to
        start at the start of the next loop. Then it will schedule itself to stop, so it just plays once. This type of
        clips also work when soloing them, they will solo just for one loop and unsolo after that.
        - **Limitation**: a Once clip is still subject to the one clip per instrument behavior of the Deluge, A Once
          clip can steal an output from other normal clips, so take that into account when you plan your performance.

#### 4.1.4 - Catch Notes

- ([#221]) The normal behavior of the Deluge is to try to keep up with 'in progress' notes when instant switching
  between clips by playing them late. However this leads to glitches with drum clips and other percussive sounds.
  Changing this setting to OFF will prevent this behavior and *not* try to keep up with those notes, leading to smoother
  instant switching between clips.
    - This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.1.5 - Grid View

- ([#251]) Add new grid session layout to song view. All functionality from (classic) row layout applies except for the
  following:
    - The data model of rows and grid mode are compatible, you can switch between them freely
    - In grid mode you will not be able to see multiple clips that are in the same section, only the first one. To make
      them visible move the clips to other sections
    - The colored column on the right are all available sections, the columns are automatically filled with the tracks
      in the same order as in arrangement mode
    - In song view hold `SONG` and turn `SELECT` knob to switch between row layout and grid layout
    - Compared to rows layout overdub recording and copying clips to arranger is currently not supported
    - Every track (column) has a random generated color that can be changed in edit mode (see below)
    - Launched clips are full color, unlaunched dimmed and during soloing all non soloed clips are greyed out
    - New default settings that can be reached pressing both `SHIFT` + `SELECT`: `MENU > DEFAULTS > UI > SONG`
        - `Layout:` Select the default layout for all new songs
        - `Grid`
            - `Default active mode:` "Selection" allows changing the mode as described below, all other settings will
              always make mode snap back to the configured one (default Selection)
            - `Select in green mode:` Enabling this will make allow holding clips in green (launch) mode to change their
              parameters like in blue mode, tradeoff is arming is executed on finger up (default on).
                - In addition, with this mode enabled, if you hold a clip and press the clip button you will enter that clip.
            - `Empty pad unarm:` Enabling will make pressing empty pads in a track unarm all playing tracks in that
              track (default off)
    - There are different interaction modes that change how the grid behaves
        - The mode can be changed by clicking on one of the colored pads in the Audition/Section column on the right
        - To permanently switch the mode click on a pad and release, to temporarily switch hold the mode pad and use the
          grid, the mode will snap back to the current permanent one
        - `Green mode`
            - All main pads behave the same as the Mute/Launch pads in rows layout (arm/immediate
              launch/mute/record/MIDI learn arm)
            - Section pads (left sidebar column) behave the same as in rows layout, in addition Shift+Section will
              immediate launch all clips in that row
        - `Blue mode`
            - All main pads behave the same as the main pads in rows layout (open/select/create/delete/MIDI learn)
            - While holding a clip it can be copied to other empty slots by clicking on them, apart from
              audio/instrument conversion clips are automatically moved to that instrument/track and converted (e.g.
              Synth to MIDI target)
            - Track color can be changed by holding any populated clip in a column and rotating `▼︎▲︎`. For fine changes
              to the color press `▼︎▲︎` while turning it.
            - Section pads (left sidebar column) will allow changing repeat count while held
- ([#970]) Streamline recording new clips while Deluge is playing
    - This assumes the Deluge is in Grid mode, you are in Green mode, the Deluge is Playing, and Recording is enabled.
    - To use this feature you will need to enable it in the menu:
        1. Enter `SETTINGS > DEFAULTS > UI > SONG > GRID > EMPTY PADS > CREATE + RECORD` and select `ENABLED`
        2. Exit Settings menu to save settings
    - The following steps enable you to quickly create and arm new clips for recording:
        1. In Grid view, make sure you are in Green mode.
        2. Press `PLAY` to start playback and press `RECORD` to enable recording.
        3. Short-press any empty pad in an existing Instrument column to create and arm a new clip for recording
        4. The new clip that was just created will be selected and start recording at the beginning of the next bar
        5. You can press `RECORD` to stop recording or press that new clip to stop recording.
        6. Repeat steps as required.
- ([#2421]) Allow true overdubbing for grid audio clips
  - Traditional guitar style looping is now possible for audio clips in grid mode. To use it monitoring must be active
  - The loop will capture all fx in the audio clip (e.g. it's recording at the end of the signal chain) and then reset the fx
  - LOOP will begin an auto extending overdub. The initial sample will loop and the clip will extend as you keep playing
  - Pressing LOOP again will end recording quantized to the original length (e.g. LOOPing on a 1-bar clip will quantize to 1 bar)
    - This works similarly to increasing loop length on an EDP style looper but without needing to set it in advance
  - LAYER will continuously layer over the existing audio without extending the loop
    - This works like an overdub on a pedal style looper
  - Only the midi loop commands work at this time but loop controls will be added to grid down the road

#### 4.1.6 - Performance View

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature
  documentation: [Performance View Documentation]
- ([#711]) Adds a new performance view, accessible from Song and Arranger View's using the Keyboard button.
    - Each column on the grid represents a different "FX" and each pad/row in the column corresponds to a different FX
      value.
    - Specifications:
        - Perform FX menu to edit Song level parameters and enable/disable editing mode.
        - 16 FX columns
            - 1 Param assigned to each column
        - 8 FX values per column
            - Long press pads (>= 100ms by default) in a column to change the value momentarily and reset it (to the value before the pad was pressed) upon pad release
            - Short press pads (< 100ms by default) in a column to change the value until you press the pad again (resetting it to the value before the pad was pressed)
            - Quickly clear all held values by pressing `HORIZONTAL ENCODER ◀︎▶︎` + `BACK` (resetting FX values back to their previous state)
        - Editing mode to edit the FX values assigned to each pad and the parameter assigned to each FX column
        - Save defaults as PerformanceView.xml file
            - Adjustable default Values assigned to each FX column via `VALUE` editing mode or PerformanceView.xml
            - Adjustable default Param assigned to each FX column via `PARAM` editing mode or PerformanceView.xml
            - Adjustable default "held pad" settings for each FX column via Performance View or PerformanceView.xml (
              simply change a held pad in Performance View and save the layout to save the layout with the held pads).
        - Load defaults from PerformanceView.xml file

### 4.1.7 - Added Master Chromatic Transpose of All Scale Mode Instrument Clips

- ([#1159]) Using the same combo as in a Synth / Midi / CV clip, press and turn `▼︎▲︎` to transpose all scale mode clips
  up or down by 1 semitone.
    - You can customize the amount of semitones that clips are transposed by, by holding Shift and pressing and turning `▼︎▲︎`. The
      display will show the number of semitones.
    - After transposing the display show the new Root Note (and Scale Name if you have an OLED display).
    - Does not affect audio clips or kit clips.
    - Works in Song Row/Grid View, Arranger View, Arranger Automation View and Performance View.

### 4.1.8 - Added VU Meter rendering to Song / Arranger View

- ([#1344]) Added `VU Meter` rendering in the sidebar in Song / Arranger / Performance Views.
    - To display the VU meter:
        - turn `AFFECT ENTIRE` on
        - select the `LEVEL/PAN` mod button
        - press the `LEVEL/PAN` mod button again to toggle the VU Meter on / off.
          - note: the `LEVEL/PAN` mod button will blink when the VU meter is on and displayed
    - The VU meter will stop rendering if you switch mod button selections, turn affect entire off, select a clip, or
      exit Song/Arranger views.
    - The VU meter will render the decibels below clipping on the grid with the colours green, yellow and red.
        - Red indicates DAC clipping and is rendered on the top row of the grid.
        - The Yellow pad below Red (second from top) indicates soft clipping.
        - If you do not want any clipping, you should target levels not exceeding -4.5 dB (e.g. not exceeding the 3rd pad from top)
        - Each row on the grid corresponds to the following decibels below clipping values:
            - y7 = hard clipping (-0.2 or higher)
            - y6 = soft clipping (-4.4 to -0.3)
            - y5 = -8.8 to -4.5 (target -4.5 or lower to avoid any clipping)
            - y4 = -13.2 to -8.9
            - y3 = -17.6 to -13.3
            - y2 = -22.0 to -17.7
            - y1 = -26.4 to -22.1
            - y0 = -30.8 to -26.5

### 4.1.9 - Song macros

Macros are a way to quickly switch playing clips without needing to go into song view.
From song view, open the `SONG MENU` and enter the `CONFIGURE MACROS` menu to edit macros.

There are 8 macro slots shown in the left sidebar.

To assign a macro, first select a macro slot and then press a clip in the grid.

Pressing the same clip multiple time cycles though different modes:

- `Clip macro`: Launch or mute the clip
  - Individual clips are green/red, so you see if they get deactivated, just like in song row view
- `Output macro`: cycle though all clips for this particular output
- `Section macro`: Launch all clips for this section

After assigning a clip to a slot, you can press the macro slot to see what clip has been assigned to it (clip will alternate between the clip's colour and white).

Inside a `CLIP TIMELINE VIEW`, hold `SONG` button and press the `LEFT SIDEBAR` to launch a macro.

In `KEYBOARD VIEW`, macros are available as a sidebar control.

`SHIFT` makes the launch immediate just like in song view.

`AFFECT ENTIRE` + `CLIP MACRO` can be used to jump to edit the clip.

### 4.2 - Clip View - General Features (Instrument and Audio Clips)

#### 4.2.1 - Filters

- ([#103] and [#336]) Adds 2 new state variable filters to both `LPF dB/OCT` and `HPF dB/OCT`. This filter has
  significantly less distortion than the ladder filters, think sequential vs. moog.
- A third parameter is added to all filter types. The shortcut is the pad under dB/Oct and it's value is kept when
  changing filter types. This param is modulatable and automatable.
    - In SVFs, the `MORPH` parameter adjusts smoothly from lowpass -> bandpass/notch -> highpass. `MORPH` is inverted in
      the `HPF MORPH` slot so that at 0 the filter is highpass and at 50 it is lowpassed.
    - In lowpass ladder filters, the `DRIVE` param smoothly increases filter drive.
    - In the highpass ladder filter `FM` adds filter FM. Try it with the resonance up!
- `FILTER ROUTE` is accessible via the `SOUND` menu only and adjusts the filter order from `HPF TO LPF`, `LPF TO HPF`,
  or `PARALLEL`.

#### 4.2.2 - Stereo Chorus

- ([#120]) New Stereo Chorus type added to Mod FX. `MOD FX DEPTH` will adjust the amount of stereo widening the effect
  has.

#### 4.2.3 - MIDI Takeover Mode

This mode affects how the Deluge handles MIDI input for learned CC controls.

- ([#170]) A new `TAKEOVER` submenu was created in the `MIDI` settings menu which consists of three modes that can be
  selected from:

  **1. `JUMP`:** This is the default mode for the Deluge. As soon as a MIDI encoder/Fader position is changed, the
  Deluge's internal encoder position/Parameter value jumps to the position of the MIDI encoder/Fader.

  **2. `PICKUP`:** The Deluge will ignore changes to its internal encoder position/Parameter value until the MIDI
  encoder/Fader's position is equal to the Deluge encoder position. After which the MIDI encoder/Fader will move in sync
  with the Deluge.

  **3. `SCALE`:** The Deluge will increase/decrease its internal encoder position/Parameter value relative to the change
  of the MIDI encoder/Fader position and the amount of "runway" remaining on the MIDI controller. Once the MIDI
  controller reaches its maximum or minimum position, the MIDI encoder/Fader will move in sync with the Deluge. The
  Deluge value will always decrease/increase in the same direction as the MIDI controller.

  **4. `RELATIVE`:** The Deluge will increase/decrease its internal encoder position/Parameter value using the relative value changes (offset) sent by the controller. The controller must be actually sending relative value changes (127 for down and 1 for up) in order for this to work.

#### 4.2.4 - Alternative Delay Types for Param Encoders (Gold encoders)

- ([#282]) Ability to select in `COMMUNITY FEATURES` menu, which parameters are controlled when you click the `DELAY`
  -related golden encoders. The default (for upper and lower encoders) is `PINGPONG` (`ON/OFF`)
  and `TYPE` (`DIGITAL`/`ANALOG`), and you can modify it so the encoder clicks change
  the `SYNC TYPE` (`EVEN, TRIPLETS, DOTTED`) and `SYNC RATE` (`OFF, WHOLE, 2ND, 4TH, ETC`) respectively.

    - This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.2.5 - Patchable Wavefolding Distortion

- ([#349]) Adds a pre filter `WAVEFOLDE` distortion, and the depth is patchable/automatable. The depth is accessible in
  both the menu and on the un-labeled pad between `SATURATION` and `LPF FREQ`. The fold has no effect when set to 0 and
  removes itself from the signal path.
    - Note that it has no effect on square waves, it's best with sines and triangles

#### 4.2.6 - Quantized Stutter

- ([#357]) Set the stutter effect to be quantized to `4TH, 8TH, 16TH, 32ND, and 64TH` rate before triggering it. Once
  you have pressed the `STUTTER`-related gold encoder, then the selected value will be the center value of the encoder
  and you can go up and down with the golden encoder and come back to the original rate by centering the encoder (LEDs
  will flash indicating it).

    - This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.2.7 - Grain FX

- ([#363]) New `GRAIN` added to Mod FX. This effect is resource-intensive, so it's suggested to use only one instance
  per song and/or resample and remove the clip or effect afterwards. As such it is turned `OFF` by default for now.

    - Parameters:
        - **`MOD RATE`:** Sets Grain Rate (0.5hz - 180hz)
        - **`MOD DEPTH`:** Controls Grain Volume / Dry Wet Mix
        - **`MOD FEEDBACK`:** Selects Grain Type (See below for values)
        - **`MOD OFFSET`:** Adjusts Grain Size (10ms - 800ms)

    - Grain Type (Presets):
        - **`0-10`:** Unison and +1 Octave (Reversed)
        - **`11-20`** Unison and -1 Octave
        - **`21-30`:** Unison and +1 Octave (Defalut)
        - **`31-40`:** 5th and +1 Octave
        - **`41-50`:** Unison and +1/-1 Octave (Tempo Sync)

    - This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.2.8 - Reverb Improvements

- ([#1065]) New reverb models are available for selection inside of the `FX > REVERB > MODEL` menu. These include:
    - Freeverb (the original Deluge reverb)
    - Mutable (an adapted version of the reverb found in Mutable Instruments' Rings module)
        - The Mutable reverb model has been set as the default reverb model for new songs. Old songs will respect the
          reverb model used with those songs.

- ([#2080]) Reverb can now be panned fully left or right. Old songs retain their reverb panning behavior, but display
  it as smaller numbers. This change also fixes an issue with reverb values displaying differently than how they were
  set.

### 4.3 - Instrument Clip View - General Features

These features were added to the Instrument Clip View and affect Synth, Kit and MIDI instrument clip types.

#### 4.3.1 - New LFO Shapes

LFO types added to the "LFO SHAPE" shortcut.

- ([#32]) **`RANDOM WALK`:** Starts at zero and walks up or down by small increments when triggered.
- ([#32]) **`SAMPLE&HOLD`:** Picks a new random value every time it is triggered.

#### 4.3.2 - New LFO Synchronization Options

Additional synchronization modes accessible through `SYNC` shortcuts for `ARP`, `LFO1`, `LFO2`, and `DELAY`:

- ([#32]) **`TPLTS`:** Synchronizes the LFO to triplet (3/2) divisions.
- ([#32]) **`DTTED`:** Synchronizes the LFO to dotted (2/3) divisions.

LFO2 can be synchronized as well, using the labelled LFO2 sync pad. Where LFO2 synchronization is relative
to each individual note onset. ([#1978])

#### 4.3.3 - Quantize & Humanize

- ([#129])
    - Press and hold an audition pad in clip view and turn the tempo encoder right or left to apply `QUANTIZE`
      or `HUMANIZE` respectively to that row.
    - Press and hold an audition pad and press and turn the tempo encoder to apply quantize or humanize to all rows.
    - The amount of quantization/humanization is shown in the display by 10% increments: 0%-100%
    - This is destructive (your original note positions are not saved) The implementation of this feature is likely to
      change in the future
    - This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.3.4 - Fill / Not Fill Modes

- ([#211]) Fill Mode is a new iteration/probability setting for notes. The `FILL` setting is at the start of the
  probability range. Notes set to `FILL` are only played when fill mode is active. These notes will be highlighted in
  bright blue color when the Fill button is held. There are two ways to activate `FILL` mode - set it as a Global MIDI
  Command and/or set it to override the front panel `SYNC-SCALING` button. For Global MIDI Commands go
  to `SETTINGS > MIDI > CMD > FILL`. To override to `SYNC-SCALING`, set `SETTINGS > COMMUNITY FEATURES > SYNC`
  to `FILL`. The orignal `SYNC-SCALING` function is moved to `SHIFT` + `SYNC-SCALING`.
- ([#994]) Additionaly, there is also the Not Fill Mode, which is right before the `FILL` setting in the probability
  selector. On the OLED Deluge, this is shown as `NOT FILL`, and on 7-SEG Deluge, this will be shown as `FILL.` (with a
  dot). Notes set to `NOT FILL` are only played when fill mode is NOT active, that is, while regular playback, and,
  contrary to `FILL` notes, will be silenced when the fill mode is active. These notes will be highlighted in bright red
  color when the Fill button is held.

#### 4.3.5 - Automation View

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature
  documentation: [Automation View Documentation]
- ([#241]) Automation Clip View is a new view that complements the existing Clip View.
    - It is accessed from within the Clip View by pressing  `CLIP` (which will blink to indicate you are in the
      Automation View).
    - You can edit Non-MPE Parameter Automation for Synth, Kit, MIDI, and Audio clips on a per step basis at any zoom
      level.
    - A `COMMUNITY FEATURES` sub-menu titled `AUTOMATION` was created to access a number of configurable settings for
      changes to existing behaviour.
    - The three changes to existing behaviour included in this feature are: Clearing Clips, Nudging Notes and Shifting a
      Clip Horizontally.
- Follow-up PR's:
    - ([#347]) Added new automatable parameters
    - ([#360]) Fixed interpolation bugs, added fine tuning for long presses, and added pad selection mode
    - ([#636]) Updated Parameter Values displayed in Automation View to match Parameter Value Ranges displayed in the
      Menu's. E.g. instead of 0 - 128, it now displays 0 - 50 (except for Pan which now displays -25 to +25 and MIDI
      instrument clips which now display 0 - 127).
    - ([#658]) Added Stutter Rate Parameter to Automation View. Note: this does not allow you to automate the triggering
      of stutter.
    - ([#681]) Added new automation community feature menu to re-instate audition pad shortcuts in the Automation Clip
      View.
        - Currently in the Instrument Clip View if you hold down an audition pad and press a shortcut pad on the grid,
          it will open the menu corresponding to that shortcut pad.
        - By default in the Automation Clip View that same behaviour of holding an audition pad and pressing a shortcut
          pad is disabled in favour of you being able to hold down an audition pad and adjust the automation lane values
          so that you can audible hear the changes to the sound while adjusting automation settings.
        - Through the community features menu, you can disable this change and re-instate the audition pad shortcuts by
          setting the community feature to "Off."
    - ([#886]) Remove parameters that have no effect in Automation View:
        - Removed Arp and Portamento from Kit Affect Entire
        - Removed Portamento from Kit Rows
    - ([#887]) Added ability to set the parameter value to the middle point between two pads pressed in a single column.
      E.g. press pads 4 and 5 to set the value to 25/50.
    - ([#887]) Updated Master Pitch parameter to display the value range of -25 to +25.
    - ([#889]) Fixed bug where automation view grid would not update / refresh when a parameter value was changed by a
      MIDI Controller that was learned to that param.
    - ([#966]) Added automation view for audio clips
    - ([#1039]) Added automation view for arranger.
        - Hold `SHIFT` and press `SONG` to enter Automation Arranger View from Arranger View.
        - Press `SONG` while in Automation Arranger View to exit back to Arranger View
        - Press `AFFECT ENTIRE` while in Automation Arranger View to go back to Automation Overview
        - Press `CROSS SCREEN` while in Automation Arranger View to activate/de-activate automatic scrolling during
          playback
        - Also, moved automation community features to defaults menu found at `SETTINGS > DEFAULTS > AUTOMATION`
            * `Automation (AUTO)`
                * `Interpolation (INTE)`
                    * When On, Interpolation is on by default in the Automation View.
                    * Note: This is just a default setting and can be overriden in the Automation View using the Shift +
                      Interpolation grid button shortcut combo.
                * `Clear (CLEA)`
                    * When On, clearing the arrangement in arranger view / a clip in the regular Instrument Clip View
                      will clear Notes and MPE, but not Automation.
                    * When On, to clear Non-MPE Automation you will need to enter the Automation Instrument Clip View.
                * `Shift Note (SHIF)`
                    * When On, shifting notes horizontally in the regular Arranger View / Instrument Clip View will
                      shift the Arrangement / Notes and MPE, but not the Automation.
                    * When On, to shift Non-MPE Automation horizontally you will need to enter the Automation Instrument
                      Clip View.
                * `Nudge (NUDG)`
                    * When On, nudging a note in the regular Instrument Clip View will nudge the Note and MPE, but not
                      the Automation.
                    * When On, to nudge Non-MPE Automation, you will need to either Shift or Manually Edit the
                      automation in the Automation Clip View.
                * `Disable Audition Pad Shortcuts (SCUT)`
                    * When On, audition pad shortcuts are disabled. Holding an audition pad and pressing a shortcut pad
                      will not activate the shortcut and will not change the selected parameter.
                    * When On, to change the selected parameter you will need to either:
                        1) use the select encoder;
                        2) use the shift + shortcut pad combo; or
                        3) go back to the automation overview;
    - ([#1083]) Updated the Automation Overview and grid shortcuts in automation view for MIDI clips to match the grid
      shortcut cc mappings for MIDI Follow. So if you want to change what CC's map to what grid shortcuts in the
      Automation View for MIDI Clips, you would need to edit the MIDIFollow.XML template for MIDI Follow mode.
    - ([#1156]) Change interpolation shortcut + Provide better integration with Deluge menu system and consistency with
      Select encoder usage.
        - Updated `AUTOMATION VIEW` to move the Interpolation shortcut to the Interpolation pad in the first column of
          the Deluge grid (second pad from the top). Toggle interpolation on/off using Shift + Interpolation shortcut
          pad. The Interpolation shortcut pad will blink to indicate that interpolation is enabled.
        - Updated `AUTOMATION VIEW` to provide access to Settings menu (hold shift + press select encoder)
        - Updated `AUTOMATION VIEW` to provide access to the Sound menu (press select encoder)
        - Updated automatable parameter editing menu's (accessed via Sound menu or Shift + parameter shortcut) to
          provide the ability to access the `AUTOMATION VIEW EDITOR` directly from the parameter menu. While in the menu
          press Clip (if you are in a clip) or Song (if you are in arranger) to open the `AUTOMATION VIEW EDITOR` while you are still in the menu. You will be able to interact with the grid to edit automation for the current parameter / patch cable selected in the menu.
    - ([#1374]) Added `AUTOMATION VIEW` for `PATCH CABLES / MODULATION DEPTH`. Simply enter the modulation menu that displays `SOURCE -> DESTINATION` and then press `CLIP` to access the `AUTOMATION VIEW EDITOR` for that specific Patch Cable / Modulation Depth.
      - ([#1607]) You can also use the `SELECT ENCODER` while in the `AUTOMATION VIEW EDITOR` to scroll to any patch cables that exist.
    - ([#1456]) Added an in-between-layer in the Deluge menu system to be able to access and interact with the `AUTOMATION VIEW EDITOR` while you are still in the menu from the regular `ARRANGER / CLIP VIEW`. When you exit the menu you will be returned to the View you were in prior to entering the menu. Press Clip (if you are in a clip) or Song (if you are in arranger) to temporarily open the `AUTOMATION VIEW EDITOR` while you are still in the menu.
    - ([#1480]) As a follow-up to [#1374] which enabled enabled patch cables to be edited in Automation View, the Automation Editor has now been modified to display param values according to whether the Param is bipolar or not. If it's a bipolar param, the grid will light up as follows:
      - Middle value = no pads lit up
      - Positive value = top 4 pads lit up according to position in middle to maximum value rnage
      - Negative value = bottom 4 pads lit up according to position in middle to minimum value range
      - Note: per the functionality added in [#887] mentioned above, you can set a param to the middle value by pressing the two pads in a column or you can use the fine tuning method with the gold encoders in or out of pad selection mode by selecting a pad and turning gold encoder.
      - To make it easier to set the middle value, functionality has been added to blink the LED indicators when you reach the middle value and it also makes it more difficult to turn the knob past the middle value as it currently did outside automation view editor.
    - ([#1898] [#2136]) Change pad selection mode shortcut.
      - Updated `AUTOMATION VIEW` to move `PAD SELECTION MODE` shortcut to the `WAVEFORM` pad in the first column of the Deluge grid (very top left pad). Toggle pad selection mode on/off using `SHIFT` + `WAVEFORM` shortcut pad. The Waveform shortcut pad will blink to indicate that pad selection mode is enabled.

#### 4.3.6 - Set Probability By Row

- ([#368]) Extends the probability system to set a row at a time. Hold an `AUDITION` pad and turn `SELECT` to change the
  whole rows probability. This is particularly useful in combination with the euclidean sequencing to get a semi random
  pattern going

#### 4.3.7 - Shorcut for "Transpose clip" is now "Nudge notes vertically for clip"

- ([#1183]) The command `SHIFT` + hold and turn `▼︎▲︎` inside a clip was causing an unexpected behavior in which all
  other clips in the song were also transposed. This has been fixed by changing this command to a "Vertical nudge"
  command, based on current clip display (either in scale or non-scale mode). This saves user from the need to "zoom
  out, copy all notes, scroll up or down, and paste all notes" to nudge notes vertically.
    - If the clip is in scale mode, all the notes are shifted up or down by one step in the scale.
    - If the clip is not in scale mode, all the notes are shifted up or down by one semitone.
    - Note: the other command for octave transposition, that is, hold and turn `▼︎▲︎`, keeps working in the same way, by
      nudging notes by one octave, regardless of the clip scale mode.

#### 4.3.8 - Advanced Arpeggiator

- ([#1198]) Added new features to the arpeggiator, which include:
    - Splitted the old `Mode` setting into separate settings: `Mode` (Off or Arpeggiator), `Octave Mode` (Up, Down,
      Up&Down, Alternate or Random) and `Note Mode` (Up, Down, Up&Down, AsPlayed or Random), so you can setup
      individually how octaves are walked and how notes are walked in the sequence.
    - The `Mode` pad shortcut is now an `Arp preset` shortcut, which will update the new 3 settings all at once:
        - `Off` will disable arpeggiator.
        - `Up` will setup Mode to `Arpeggiator`, Octave Mode to `Up` and Note Mode to `Up`.
        - `Down` will setup Mode to `Arpeggiator`, Octave Mode to `Down` and Note Mode to `Down`.
        - `Both` will setup Mode to `Arpeggiator`, Octave Mode to `Alternate` and Note Mode to `Up`.
        - `Random` will setup Mode to `Arpeggiator`, Octave Mode to `Random` and Note Mode to `Random`.
        - `Custom` will setup Mode to `Arpeggiator`, and enter a submenu to let you edit Octave Mode and Note Mode.
    - **`Mode (MODE):`**
        - `Off` disables the arpeggiator.
        - `Arpeggiator` (ARP) enables the arpeggiator.
    - **`Octave Mode (OMOD):`**
        - `Up` (UP) will walk the octaves up.
        - `Down` (DOWN) will walk the octaves down.
        - `Up & Down` (UPDN) will walk the octaves up and down, repeating the highest and lowest octaves.
        - `Alternate` (ALT)  will walk the octaves up, and then down reversing the Notes pattern (without
          repeating notes). Tip: Octave Mode set to Alternate and Note Mode set to Up is equivalent to
          the old `Both` mode.
        - `Random` (RAND) will choose a random octave every time the Notes pattern has played.
          Tip: Set also Note Mode to Random to have the equivalent to the old `Random` mode.
    - **`Note Mode (NMOD):`**
        - `Up` (UP) will walk the notes up.
        - `Down` (DOWN) will walk the notes down. Tip: this mode also works in conjunction with Octave Mode
          Alternate, which will walk all the notes and octaves all the way down, and then up reversing it.
        - `Up & Down` (UPDN) will walk the notes up and down, repeating the highest and lowest notes.
        - `As played` (PLAY) will walk the notes in the same order that they were played. Tip: this mode
          also works in conjunction with Octave Mode Alternate, which will walk all the notes and octaves
          all the way up (with notes as played), and then down reversing the order of played notes.
        - `Random` (RAND) will choose a random note each time. If the Octave Mode is set to something
          different than Random, then the pattern will play, in the same octave, the same number of random
          notes as notes are in the held chord and then move to a different octave based on the Octave Mode.
          Tip: Set also Octave Mode to Random to have the equivalent to the old `Random` mode.
    - **`Rhythm`** (RHYT) (unpatchet parameter, assignable to golden knobs):
      This parameter will play silences in some of the steps. This menu option show zeroes
      and dashes, "0" means "play note", and "-" means "don't play note" (or play a silence).
      The available options are:
      <details>
      <summary>Rhythm Options</summary>
        <ul>
          <li> 0: None</li>
          <li> 1: 0--</li>
          <li> 2: 00-</li>
          <li> 3: 0-0</li>
          <li> 4: 0-00</li>
          <li> 5: 00--</li>
          <li> 6: 000-</li>
          <li> 7: 0--0</li>
          <li> 8: 00-0</li>
          <li> 9: 0----</li>
          <li>10: 0-000</li>
          <li>11: 00---</li>
          <li>12: 0000-</li>
          <li>13: 0---0</li>
          <li>14: 00-00</li>
          <li>15: 0-0--</li>
          <li>16: 000-0</li>
          <li>17: 0--0-</li>
          <li>18: 0--00</li>
          <li>19: 000--</li>
          <li>20: 00--0</li>
          <li>21: 0-00-</li>
          <li>22: 00-0-</li>
          <li>23: 0-0-0</li>
          <li>24: 0-----</li>
          <li>25: 0-0000</li>
          <li>26: 00----</li>
          <li>27: 00000-</li>
          <li>28: 0----0</li>
          <li>29: 00-000</li>
          <li>30: 0-0---</li>
          <li>31: 0000-0</li>
          <li>32: 0---0-</li>
          <li>33: 000-00</li>
          <li>34: 0--000</li>
          <li>35: 000---</li>
          <li>36: 0000--</li>
          <li>37: 0---00</li>
          <li>38: 00--00</li>
          <li>39: 0-00--</li>
          <li>40: 000--0</li>
          <li>41: 0--00-</li>
          <li>42: 0-0-00</li>
          <li>43: 00-0--</li>
          <li>44: 000-0-</li>
          <li>45: 0--0-0</li>
          <li>46: 0-000-</li>
          <li>47: 00---0</li>
          <li>48: 00--0-</li>
          <li>49: 0-0--0</li>
          <li>50: 00-0-0</li>
        </ul>
      </details>
    - **`Sequence Length`** (LENG) (unpatchet parameter, assignable to golden knobs):
        - If set to zero, the arpeggiator pattern will play fully.
        - If set to a value higher than zero, the pattern will play up to the set number of notes, and then
          reset itself to start from the beginning. Tip: You can use this in combination with the Rhythm parameter
          to create longer and more complex rhythm patterns.
    - **`Ratcheting:`** There are two new parameters (unpatched, assignable to golden knobs), to control how notes
      are ratcheted. A ratchet is when a note repeats itself several times in the same time interval that the
      original note has to play.
        - `Ratchet Amount` (RATC): this will set the maximum number of ratchets that an arpeggiator step
          could have (each step will randomize the number of ratchet notes between 1 and max value).
            - From values 0 to 4, no ratchet notes
            - From 5 to 19, up to 2 ratchet notes
            - From 20 to 34, up to 4 ratchet notes
            - From 35 to 50, up to 8 ratchet notes
        - `Ratchet Probability` (RPRO): this sets how likely a step is to be ratcheted
            - Being 0 (0%), no ratchets at all
            - And 50 (100%), all notes will evaluate to be ratcheted.
    - **`MPE`** settings:
      - `Velocity`: if you have an MPE keyboard you may want to enable this. It will allow you to control the
      velocity of each new arpeggiated note by applying different pressure (aftertouch) or slide (Y) on the keys.

#### 4.3.9 - Velocity View

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature documentation: [Velocity View Documentation]
- ([#2046]) Added `VELOCITY VIEW`, accessible from `AUTOMATION VIEW OVERVIEW` by pressing the `VELOCITY` shortcut or from `INSTRUMENT CLIP VIEW` by pressing `AUDITION PAD + VELOCITY`. Velocity View enables you to edit the velocities and other parameters of notes in a single note row using a similar interface to `AUTOMATION VIEW`.

#### 4.3.10 - Enhanced Note Probability, Iterance and Fill

- ([#2641], [#2751]) Enhanced existing note probability, iteration and fill function functionality by enabling you to use each type independently. This means that you can now apply probability to iteration and fill and you can also apply iteration to fill.
  - To edit probability, hold a note / audition pad and turn the select encoder to the left to display current probability value / set new probability value.
  - To edit iterance, hold a note / audition pad and turn the select encoder to the right to display current iterance value / set new iterance value.
  - The iteration is now also customizable with custom iteration steps. If you scroll the iteration parameter all the way to the right, you will see the `CUSTOM` option. If you click the `SELECT` encoder, a new menu will appear to select the `DIVISOR` parameter (you can select from 1 to 8), and also as many `ITERATION #` toggles as `DIVISOR` is set, to allow you to activate or deactivate each iteration step.
  - To edit fill, you need to access the new note and note row editor menu's.

#### 4.3.11 - Added New Note and Note Row Editor Menu's
- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature documentation: [Note / Note Row Editor Documentation]
- ([#2641]) Added new note and note row editor menu's to edit note and note row parameters.
  - Hold a note and press the select encoder to enter the note editor menu. While in the note editor menu, the selected note will blink. You can select other notes by pressing the notes on the grid.
  - Hold a note row audition pad and press the select encoder to enter the note row editor menu. While in the note row editor menu, the select note row audition pad will blink. You can select other note row's by pressing the note row audition pad.

#### 4.3.12 - Auto Load sample when browsing

- ([#2676]) When browsing for sample to assign to a Synth or to a Kit Row, you can now enable auto-load of previewed samples. To do this, open the sample browser (`SHIFT + BROWSE`), and engage `AUTO-LOAD` by pressing `LOAD`. While the `LOAD` LED is on, all the samples that you preview will automatically be loaded to the instrument as if you had confirmed the selection. This makes previewing single cycle waveforms way more convenient, for example.

### 4.4 - Instrument Clip View - Synth/MIDI/CV Clip Features

#### 4.4.1 - Keyboard View

##### 4.4.1.1 - Row Step

- ([#46]) Row Step affects the offset between rows, `ROW STEP` is now configurable by holding `SHIFT` and
  turning `◀︎▶︎`. The default Deluge Keyboard note offset is 5. A row step of 12 will produce a layout in which any
  given note's octave is directly above or below it. The setting is saved per clip in the song file.

##### 4.4.1.2 - Keyboard API and General Improvements

- ([#138] and [#337])
    - Users can switch between layouts by pressing `KEYBOARD` and turning `SELECT`
    - Keyboard mode allows freely switch between all types (`SYNTH, KIT, MIDI, CV`) automatically getting the first
      compatible layout
    - Drum trigger edge sizes in Drums layout for kits can now be changed between 1 and 8 with `SHIFT` + turn `◀︎▶︎`
    - A new in-key only layout that removes out of scale buttons/notes.
    - New way to change `SCALE` in keyboard mode: Hold `SCALE` and press `SELECT`
    - New way to change scale `ROOT NOTE` in keyboard mode: Hold `SCALE` and turn `SELECT`
    - A new menu to select the default Keyboard Layout has been added in `MENU -> DEFAULTS -> UI -> KEYBOARD -> LAYOUT`

##### 4.4.1.3 - Highlight Incoming Notes

- ([#250]) New community feature makes In-Key and Isometric layout display incoming MIDI notes with their velocity.
    - This feature is `ON` by default and can be set to `ON` or `OFF` in the `COMMUNITY FEATURES` menu (
      via `SETTINGS > COMMUNITY FEATURES`).

##### 4.4.1.4 - Display Norns Layout

- ([#250]) Enables keyboard layout which emulates a monome grid for monome norns using Midigrid mod on norns by
  rendering incoming MIDI notes on channel 16 as white pads using velocity for pad brightness.
    - This feature is `OFF` by default and can be set to `ON` or `OFF` in the `COMMUNITY FEATURES` menu (
      via `SETTINGS > COMMUNITY FEATURES`).
    - Deluge has multiple USB ports, 3 as of this writing. Use Deluge 1 as the device on norns.
    - The Midigrid mod translates MIDI notes between norns and Deluge to use the grid as a controller for a norns
      script. Midigrid sends MIDI notes on channel 16 from norns to Deluge to light up grid LEDs. When a pad is pressed
      on Deluge, it sends out a MIDI note on channel 16 to norns. This means that Deluge's usual way of learning a MIDI
      controller to a synth clip will be constantly interrupted by the stream of MIDI notes coming in on channel 16 from
      norns. To learn external MIDI controls to Deluge while using the Midigrid mod, first stop the running script on
      norns, turn off the Midigrid mod in the mod menu, or determine another method to pause the grid updating MIDI
      messages from norns.
    - The functionality of the grid changes with each norns script.

  **1.** Connect Deluge to norns with a USB cable for MIDI.
  **2.** Install [Midigrid](https://llllllll.co/t/midigrid-use-launchpads-midi-grid-controllers-with-norns/42336/) on
  your norns, turn on the mod, set to 128 grid size.
  **3.** Turn on two features in the `COMMUNITY FEATURES` menu (via `SETTINGS > COMMUNITY FEATURES`): "Highlight
  Incoming Notes" (HIGH) and "Norns Layout" (NORN) both set to ON.
  **4.** Create a MIDI clip on Deluge by pressing `MIDI` button in Clip View. Set MIDI output for the clip to channel 16
  by turning the `SELECT` encoder.
  **5.** Select the keyboard layout on the MIDI clip. Press and hold keyboard button and turn `SELECT` encoder to
  select "Norns Layout" (NORN).
  **6.** Select a [script](https://norns.community/) on norns that supports grid controls (awake, boingg,
  rudiments, ... ).
  **7.** The grid LEDs should light up indicating that norns is sending MIDI notes out on channel 16 to Deluge. Press a
  pad to see a change on norns indicating Deluge is sending MIDI notes out on channel 16.

#### 4.4.1.5 - Selectable Sidebar Controls

- ([#901]) In the keyboard view, a control can be mapped to each column of pads in the sidebar. You can change which
  control is selected by holding the top pad and turning the horizontal encoder. Controls include:
    - **`Velocity (VEL - Red):`** Low velocity is on the bottom pad and high on the top pad scaled linearly.
      The range can be adjusted by holding the top or bottom pad and scrolling the vertical encoder.
      Hold a pad down to change the velocity of notes played while held. The velocity will return to
      default when you release the pad. Short press a pad to set a new default velocity.
      This is called "Momentary" mode and is the default. You can disable it from the main menu:
      Defaults -> UI -> Keyboard -> Sidebar controls (CTRL) -> Momentary Velocity (MVEL)
    - **`Modwheel (MOD - Blue):`** This controls the modwheel (Y) setting and has the same
      controls as the velocity control. Its behaviour is set to "Momentary" by default. You can
      disable it from the main menu:
      Defaults -> UI -> Keyboard -> Sidebar controls (CTRL) -> Momentary Modwheel (MMOD)
    - **`Chords (CHRD - Green):`** Press and hold or tap to either temporarily set a
      chord or change the default chord. Any note you play will be interpreted as the root note
      and the remaining notes will be played along with it. The default chord is none.
      You can get back to none by short pressing the current chord. Chords include 5th, Sus2,
      Minor, Major, Sus4, Minor7, Dom7, Major7. All are in closed root position. (Note: this option is not available while in either of the chord keyboard layouts.)
    - **`Song Chord Memory (CMEM - Cyan):`** Hold a chord down and press a pad to remember the chord. Press
      that pad again to play it. You can play over the top of your saved chords. To clear a chord,
      press shift and the pad you want to clear. Chord memory is shared across all song clips and it
      is saved in the song file.
    - **`Clip Chord Memory (CCME - Purple):`** Hold a chord down and press a pad to remember the chord. Press
      that pad again to play it. You can play over the top of your saved chords. To clear a chord,
      press shift and the pad you want to clear. Clip chord memory is available only to the current instrument.
    - **`Scale Mode (SMOD - Yellow):`** Press and hold a pad to temporarily change the scale of the
      keyboard to the selected scale mode. Tap a scale mode to make it the new default. The scale
      pads will default to the first 7 scale modes, but you can change any pad to any scale by
      holding it down and turning the vertical encoder. If the scale that is going to be set
      can't fit/transpose the existing notes from your clips, screen will show `Can't`.
    - **`Song Macro Mode (SONG - various):`** Activate [Song macros](#419---song-macros).

- ([#2174]) With the addition of the new Keyboard Sidebar Controls, the default behaviour of being able to immediately exit the menu by pressing a sidebar pad while in Keyboard View was removed. To accomodate users that still wish to be able to exit the menus immediately by pressing a sidebar pad, a new community feature toggle has been added (`Enable KB View Sidebar Menu Exit (EXIT)`) which will enable you to immediately exit the menu using the top left sidebar pad if you are in the `SETTINGS` or `SOUND` menu for `KEYBOARD VIEW`.

#### 4.4.1.6 - Display Chord Keyboard Layout

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature
  documentation: [Chord Keyboard Documentation]

- ([#2475]) Enables `CHORD` keyboard, out of scale view with two modes, Column mode (providing in-scale harmonically similar chords for substitutions) and Row Mode (based largely on the Launchpad Pro's chord mode that spreads notes out on a row to allow users to build and play interesting chords across the rows). After enabling the `Chord Keyboards (CHRD)` community feature (see the [Community Features Menu](#5-community-features-menu-aka-runtime-settings) for more information), the `CHORD` keyboard is accessible the same way as other instrument supporting keyboards. While on `SYNTH`, `MIDI`, or `CV`, press `KEYBOARD` to enter into a `KEYBOARD` view, and then press and hold `KEYBOARD` + turn `SELECT` to cycle through layouts to find the `CHORD` keyboard layout.

- As the UI and implementation is still experimental, a community setting has to be activated to access the `CHORD` keyboard. See the [Community Features Menu](#5-community-features-menu-aka-runtime-settings) for more information.

#### 4.4.1.7 - Display Chord Library Keyboard Layout

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature
  documentation: [Chord Keyboard Documentation]

- ([#2385]) Enables `CHORD LIBRARY` keyboard layout which displays a library of chords on the pads. After enabling the `Chord Keyboards (CHRD)` community feature (see the [Community Features Menu](#5-community-features-menu-aka-runtime-settings) for more information), the `CHORD LIBRARY` keyboard is accessible the same way as other instrument supporting keyboards. While on `SYNTH`, `MIDI`, or `CV`, press `KEYBOARD` to enter into a `KEYBOARD` view, and then press `SELECT` + `KEYBOARD` to cycle through layouts to find the `CHORD LIBRARY` keyboard layout.

- As the UI and implementation is still experimental, a community setting has to be activated to access the `CHORD LIBRARY` keyboard. See the [Community Features Menu](#5-community-features-menu-aka-runtime-settings) for more information.

#### 4.4.2 - Scales

- ([#991]) Added new scales for instrument clips.
    - The new set of scales is:
        - `7-note scales:` Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Melodic Minor, Hungarian Minor,
          Marva (Indian), Arabian
        - `6-note scales:` Whole Tone, Blues
        - `5-note scales:` Pentatonic Minor, Hirajoshi (Japanese)
    - You rotate through them the same way as before, using Shift + Scale.
    - **Improvement:** when you exit Scale mode, and enter Scale mode again, if there was already a selected Scale, and
      it fits the notes you have entered, the Deluge will prefer that scale, instead of trying to change your scale or
      root note to other scale.
    - **Migrating from previous firmwares:** the new Default Scale setting is saved in a different memory slot. The
      Deluge will import the selected scale from the old location, resetting the old location value to "None".
    - **Limitation:** For being able to change your scale to a scale with less notes (for example, from Arabic to Whole
      Tone, or from Blues to Pentatonic), in order for the Deluge to be able to transpose them, the notes entered among
      all clips in the song must fit the new scale. If you have added more notes that what can fit in the new scale, the
      Deluge will omit those scales, and cycle back to the beginning of the Scales list (that is, going back to the
      Major scale).

- ([#2365]) Added learning a user specified scale.
    - Hold `LEARN` and press `SCALE` while in clip view. Notes from current clip & all scale mode clips are learned as the "USER"
      scale. This scale is part of the normal scale rotation, accessible with `SHIFT` + `SCALE`, and saved as part of the song.
      If another user scale is learned, the previous one is overwritten: currently each song can only have one user scale.
    - If you enter scale mode from a chromatic clip, and the implied scale cannot be represented by any of the existing
      preset scales, it will be learned as a user scale, overwriting the previous `USER` scale.
    - NOTE: extended support for user scales is planned, allowing multiple user scales to be learned, saved, and loaded. Soon!

- ([#2376]) Added `ACTIVE SCALES` menu.
    - `SONG > ACTIVE SCALES` toggles scales on and off from the `SHIFT + SCALE` rotation for the current song. Active scales
      are saved as part of the song. On 7-segment display dot indicates that the named scale is active, lack of dot indicates
      it has been disabled.
    - `DEFAULTS > SCALE > ACTIVE SCALES` sets the active scales for new songs. When `RANDOM` is set as
      `DEFAULTS > SCALE > INIT SCALE`, the random scale is selected from default active scales.

### 4.5 - Instrument Clip View - Synth/Kit Clip Features

#### 4.5.1 - Mod Matrix

- ([#157]) Add a `MOD MATRIX` entry to the `SOUND` menu which shows a list of all currently active modulations.

#### 4.5.2 - Unison Stereo Spread

- ([#223]) The unison parts can be spread accross the stereo field. Press `SELECT` when in the `UNISON NUMBER` menu to
  access the new unison spread parameter.

#### 4.5.3 - Waveform Loop Lock

- ([#293]) When a sample has `LOOP START` (CYAN) and `LOOP END` (MAGENTA) points set, holding down `LOOP START` and
  tapping `LOOP END` will `LOCK` the points together. Moving one will move the other, keeping them the same distance
  apart. Use the same process to unlock the loop points. Use `SHIFT` + turn `◀︎▶︎` to double or half the loop length.
- **WARNING: If you shift loop points after being locked there is a chance when you delete a loop point, a loop point
  will exist outside of the SAMPLE START or SAMPLE END range and cause a crash. (Known Bug)**

#### 4.5.4 - Copy row(s)

- ([#805]) If you hold down the audition pad(s) while pressing the shortcut to "copy notes", only the auditioned rows
  will be copied. If you were to normally paste these rows, no behavior is changed from the current implementation. It's
  exactly the same result as deleting the notes on any rows you didn't have held down before copying.

#### 4.5.5 - Paste gently

- ([#805]) Pressing `SHIFT` + `CROSS SCREEN` + `◀︎▶︎` will paste any notes on the clipboard _over_ the existing notes
  i.e. the existing notes will not be deleted and the clipboard contents will be added to the existing notes.
  Positioning/scale/timing semantics have not changed, only whether the notes are cleared before pasting.

#### 4.5.6 - Configure Note Row Play Direction
- ([#1739]) Added Synth/MIDI/CV clip configuration of note row play direction. Hold audition pad while entering the play direction menu to set the play direction for the selected note row. While in the note row play direction menu, you can select other note rows to quickly set the play directiom for multiple note rows.

#### 4.5.7 - DX7 Synth type

- For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature
  documentation: [DX7 Synth Documentation]

- ([#1114]) A new synth type is added fully compatible with DX7 patches, including editing of all DX7 parameters. This is implemented
as an oscillator type within the subtractive engine, so it can be combined with filters and other features of this engine.

  - Patches can be imported from the common 32-patch bank syx-file format, and afterwards saved as SYNTH presets or as part of songs.

  - As the UI and implementation is still experimental, a community setting has to be activated to create new DX7 patches. See the separate document for details.

### 4.6 - Instrument Clip View - Kit Clip Features

#### 4.6.1 - Keyboard View

- ([#112]) All-new use for the `KEYBOARD` button in kit clips, uses the main pad grid for MPC-style 16 level
  playing. `◀︎▶︎` scrolls by one pad at a time, allowing positioning drums left to right, and `▼︎▲︎` jumps vertically by
  rows.
- Follow-up PR: ([#317]) Fixed the issue where audition pads would remain illuminated after pressing pads 9 to 16 and
  then returning to the Clip View.

#### 4.6.2 - Drum Randomizer / Load Random Samples

- ([#122]) Pressing `AUDITION` + `RANDOM` on a drum kit row will load a random sample from the same folder as the
  currently enabled sample and load it as the sound for that row.
    - This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.
- ([#955]) Pressing `SHIFT` + `RANDOM` randomizes all non-muted drum kit rows.

#### 4.6.3 - Manual Slicing / Lazy Chop

- ([#198]) In the `SLICE` menu, press `◀︎▶︎` to enter `MANUAL SLICING`. When you press the green pad in the bottom left,
  it starts playing the first slice, and pressing any empty pad creates a new slice at the time you've pressed it.
  Turning `SELECT` to the left or pressing the `PAD` + `DELETE` button allows you to delete a slice.

- Turning `◀︎▶︎` allows you to adjust the start point of the slice. Additionally, turning `▼︎▲︎` transposes the slice by
  a semitone.

- Follow-up PR: ([#210]) Changed to stop audio preview (played by the sample browser) when entering manual slicer mode.

#### 4.6.4 - Batch Delete Kit Rows

- ([#234]) While you can delete a kit row by holding a Note in a row and pressing `SAVE/DELETE`,
  the `DELETE UNUSED KIT ROWS` feature allows you to batch delete kit rows which does not contain any notes, freeing
  kits from unused sounds (which take some precious RAM). While inside a Kit, hold `KIT` + `SHIFT` + `SAVE/DELETE`. A
  confirmation message will appear: "DELETE UNUSED ROWS". This command is not executed if there are no notes at all in
  the kit.
    - This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

### 4.7 Instrument Clip View - Midi Clip Features

#### 4.7.1 - Save/Load MIDI Presets

- ([#1390]) Allows saving and loading midi presets. They end up in a new folder named MIDI.
  - Note: The information that is saved is the MIDI channel selection, and the assignments of CC parameters to golden knobs.

### 4.8 Audio Clip View - Features

#### 4.8.1 - Shift Clip

- ([#141]) Holding `▼︎▲︎` down while turning `◀︎▶︎` will shift the waveform of an Audio clip, similar to Instrument
  clips.

### 4.9 Third Party Device Integration

This is largely on the development side and created the start of a system of modules and hook points for enabling
actions on the Deluge to signal third-party equipment over hosted USB. To start things off this includes some support
for the Lumi Keys Studio Edition, described below.

### 4.9.1 Lumi Keys Studio Edition

- ([#812]) When using the Deluge as a USB Midi Host and attaching a Lumi Keys Studio Edition, the keys will go dark
  until it is learned to a clip. Once learned to a clip, the keys will match the colour of the currently visible
  octave (compatible Deluge scales that will match Lumi scales are Major, Minor, Dorian, Phrygian, Lydian, Mixolydian,
  Locrian, Harmonic Minor, Arabian, Whole Tone, Blues, and Pentatonic Minor).
    - The lit and darkened keys will be aligned with the selected root and scale, so long as the selected scale is one
      of the builtin scales supported by the Lumi.
    - While Lumi has limited options for MPE separation, it will be configured to align with the dominant MPE range
      defined on the Deluge (upper or lower dominant).

## 5. Community Features Menu (aka Runtime Settings)

In the main menu of the Deluge (accessed by pressing both "SHIFT" + the "SELECT" encoder) there is
the `COMMUNITY FEATURES` (OLED) or `FEAT` (7SEG) entry which allows you to turn features on and off as needed. Here is a
list of all options as listed in OLED and 7SEG displays and what they do:

* `Drum Randomizer (DRUM)`
    * When On, the "AUDITION + RANDOM" shortcut is enabled.
* `Fine Tempo Knob (TEMP)`
    * When On, the Fine Tempo change option is enabled.
* `Quantize (QUAN)`
    * When On, the Note Quantize shortcut is enabled.
* `Mod. Depth Decimals (MOD.)`
    * When On, Modulation Resolution is increased.
* `Catch Notes (CATC)`
    * When Off, existing "Catch Notes" behaviour is disabled.
* `Delete Unused Kit Rows (UNUS)`
    * When On, the Delete Unused Kit Rows shortcut (hold "KIT" then "SHIFT" + "SAVE/DELETE") is enabled.
* `Alternative Golden Knob Delay Params (DELA)`
    * When On, it changes the behaviour of the Mod Encoder button action from the default (PingPong and Type) to the
      alternative params (SyncType and SyncLevel).
* `Stutter Rate Quantize (STUT)`
    * When On, the ability to set the stutterer effect to be quantized to 4th, 8th, 16th, 32nd, and 64th rate when
      selecting it is enabled.
* `Allow Insecure Develop Sysex Messages (SYSX)`
    * When On, the ability to load firmware over USB is enabled.
* `Sync Scaling Action (SCAL)`
    * When set to Fill, it changes the behaviour of the "SYNC-SCALING" button is changed to activate "FILL" mode. The
      original Sync Scaling button function is moved to "SHIFT" + "SYNC-SCALING".
* `Highlight Incoming Notes (HIGH)`
    * When On, In-Key and Isometric Keyboard layouts display incoming MIDI notes with their velocity.
* `Display Norns Layout (NORN)`
    * When On, enables keyboard layout which emulates monome grid for monome norns using midigrid mod where incoming
      midi
      notes on channel 16 are rendered as white pads using velocity for brightness.
* `Sticky Shift (STIC)`
    * When On, tapping shift briefly will enable sticky keys while a long press will keep it on. Enabling this setting
      will automatically enable "Light Shift" as well.
* `Light Shift (LIGH)`
    * When On, the Deluge will illuminate the shift button when shift is active. Mostly useful in conjunction with
      sticky
      shift.
* `Grain FX (GRFX)`
    * When On, `GRAIN` will be a selectable option in the `MOD FX TYPE` category. Resource intensive, recommended to
      only use one instance per song or resample and remove instance afterwards.
* `Enable DX7 Engine (DX7)`
    * When On, enables access to the DX7 synth engine (see [DX7 Synth Documentation]).
* `Emulated Display (EMUL)`
    * This allows you to emulate the 7SEG screen on a deluge with OLED hardware screen.
    * In "Toggle" mode, the "SHIFT" + "LEARN" + "AFFECT-ENTIRE" combination can used to switch between screen types at
      any time.
    * With the "7SEG" mode, the deluge will boot with the emulated display.
    * This option is technically available also on deluge with 7SEG hardware. But as you need an external display to
      render the OLED screen, it is of more limited use.
* `KB View Sidebar Menu Exit (EXIT)`
    * When On, while in the `SETTINGS` or `SOUND` menu of `KEYBOARD VIEW`, pressing the top left sidebar pad will immediately exit the menu.
* `Launch Event Playhead (PLAY)`
    * When On, a red and white playhead will be rendered in Song Grid and Performance Views that let's you know that a maximum of one bar (16 notes) is remaining before a clip or section launch event is scheduled to occur.
* `Chord Keyboards (CHRD)`
    * When On, enables the `CHORD` keyboard, which allows playing in-scale chords in a column or accross pads in a row as well as enables the `CHORD LIBRARY` keyboard layout which allows playing a library of chords on the pads. See [Chord Keyboard Layout](#4416---display-chord-keyboard-layout) and [Chord Library Keyboard Layout](#4417---display-chord-library-keyboard-layout) for more information.
* `Alternative Playback Start Behaviour (STAR)`
    * When On, the behaviour of playback start shortcuts changes as follows:
      * With playback off, pressing `PLAY` will start playback from the current grid scroll position
      * With playback off, pressing `HORIZONTAL ENCODER ◀︎▶︎` + `PLAY` will start playback from the start of the arrangement or clip
* `Grid View Loop Pads (LOOP)`
    * When On, two pads (Red and Magenta) in the `GRID VIEW` sidebar will be illuminated and enable you to trigger the `LOOP` (Red) and `LAYERING LOOP` (Magenta) global MIDI commands to make it easier for you to loop in `GRID VIEW` without a MIDI controller.

## 6. Sysex Handling

Support for sending and receiving large sysex messages has been added. Initially, this has been used for development
centric features.

- ([#174] and [#192]) Send the contents of the screen to a computer. This allows 7SEG behavior to be evaluated on OLED
  hardware and vice versa
- ([#215]) Forward debug messages. This can be used as an alternative to RTT for print-style
  debugging. (`./dbt sysex-logging <port_number>`)
- ([#295]) Load firmware over USB. As this could be a security risk, it must be enabled in community feature
  settings. (`./dbt loadfw <port_number> <hex_key> <firmware_file_path>`)

## 7. Compiletime settings

This list includes all preprocessor switches that can alter firmware behaviour at compile time and thus require a
different firmware

* ENABLE_SYSEX_LOAD

  Allow loading firmware over sysex as described above

* ENABLE_MATRIX_DEBUG

  Enable additional debug output for matrix driver when pads are pressed

* FEATURE_...

  Description of said feature, first new feature please replace this

[#17]: https://github.com/SynthstromAudible/DelugeFirmware/pull/17

[#32]: https://github.com/SynthstromAudible/DelugeFirmware/pull/32

[#46]: https://github.com/SynthstromAudible/DelugeFirmware/pull/46

[#47]: https://github.com/SynthstromAudible/DelugeFirmware/pull/47

[#103]: https://github.com/SynthstromAudible/DelugeFirmware/pull/103

[#112]: https://github.com/SynthstromAudible/DelugeFirmware/pull/112

[#118]: https://github.com/SynthstromAudible/DelugeFirmware/pull/118

[#120]: https://github.com/SynthstromAudible/DelugeFirmware/pull/120

[#122]: https://github.com/SynthstromAudible/DelugeFirmware/pull/122

[#125]: https://github.com/SynthstromAudible/DelugeFirmware/pull/125

[#129]: https://github.com/SynthstromAudible/DelugeFirmware/pull/129

[#138]: https://github.com/SynthstromAudible/DelugeFirmware/pull/138

[#141]: https://github.com/SynthstromAudible/DelugeFirmware/pull/141

[#147]: https://github.com/SynthstromAudible/DelugeFirmware/pull/147

[#157]: https://github.com/SynthstromAudible/DelugeFirmware/pull/157

[#163]: https://github.com/SynthstromAudible/DelugeFirmware/pull/163

[#170]: https://github.com/SynthstromAudible/DelugeFirmware/pull/170

[#174]: https://github.com/SynthstromAudible/DelugeFirmware/pull/174

[#178]: https://github.com/SynthstromAudible/DelugeFirmware/pull/178

[#192]: https://github.com/SynthstromAudible/DelugeFirmware/pull/192

[#196]: https://github.com/SynthstromAudible/DelugeFirmware/pull/196

[#198]: https://github.com/SynthstromAudible/DelugeFirmware/pull/198

[#200]: https://github.com/SynthstromAudible/DelugeFirmware/pull/200

[#210]: https://github.com/SynthstromAudible/DelugeFirmware/pull/210

[#211]: https://github.com/SynthstromAudible/DelugeFirmware/pull/211

[#212]: https://github.com/SynthstromAudible/DelugeFirmware/pull/212

[#215]: https://github.com/SynthstromAudible/DelugeFirmware/pull/215

[#220]: https://github.com/SynthstromAudible/DelugeFirmware/pull/220

[#221]: https://github.com/SynthstromAudible/DelugeFirmware/pull/221

[#223]: https://github.com/SynthstromAudible/DelugeFirmware/pull/223

[#234]: https://github.com/SynthstromAudible/DelugeFirmware/pull/234

[#241]: https://github.com/SynthstromAudible/DelugeFirmware/pull/241

[#250]: https://github.com/SynthstromAudible/DelugeFirmware/pull/250

[#251]: https://github.com/SynthstromAudible/DelugeFirmware/pull/251

[#282]: https://github.com/SynthstromAudible/DelugeFirmware/pull/282

[#293]: https://github.com/SynthstromAudible/DelugeFirmware/pull/293

[#295]: https://github.com/SynthstromAudible/DelugeFirmware/pull/295

[#317]: https://github.com/SynthstromAudible/DelugeFirmware/pull/317

[#336]: https://github.com/SynthstromAudible/DelugeFirmware/pull/336

[#337]: https://github.com/SynthstromAudible/DelugeFirmware/pull/337

[#339]: https://github.com/SynthstromAudible/DelugeFirmware/pull/339

[#347]: https://github.com/SynthstromAudible/DelugeFirmware/pull/347

[#349]: https://github.com/SynthstromAudible/DelugeFirmware/pull/349

[#357]: https://github.com/SynthstromAudible/DelugeFirmware/pull/357

[#360]: https://github.com/SynthstromAudible/DelugeFirmware/pull/360

[#363]: https://github.com/SynthstromAudible/DelugeFirmware/pull/363

[#368]: https://github.com/SynthstromAudible/DelugeFirmware/pull/368

[#395]: https://github.com/SynthstromAudible/DelugeFirmware/pull/395

[#512]: https://github.com/SynthstromAudible/DelugeFirmware/pull/512

[#630]: https://github.com/SynthstromAudible/DelugeFirmware/pull/630

[#636]: https://github.com/SynthstromAudible/DelugeFirmware/pull/636

[#653]: https://github.com/SynthstromAudible/DelugeFirmware/pull/653

[#658]: https://github.com/SynthstromAudible/DelugeFirmware/pull/658

[#681]: https://github.com/SynthstromAudible/DelugeFirmware/pull/681

[#683]: https://github.com/SynthstromAudible/DelugeFirmware/pull/683

[#711]: https://github.com/SynthstromAudible/DelugeFirmware/pull/711

[#805]: https://github.com/SynthstromAudible/DelugeFirmware/pull/805

[#812]: https://github.com/SynthstromAudible/DelugeFirmware/pull/812

[#837]: https://github.com/SynthstromAudible/DelugeFirmware/pull/837

[#886]: https://github.com/SynthstromAudible/DelugeFirmware/pull/886

[#887]: https://github.com/SynthstromAudible/DelugeFirmware/pull/887

[#888]: https://github.com/SynthstromAudible/DelugeFirmware/pull/888

[#889]: https://github.com/SynthstromAudible/DelugeFirmware/pull/889

[#901]: https://github.com/SynthstromAudible/DelugeFirmware/pull/901

[#934]: https://github.com/SynthstromAudible/DelugeFirmware/pull/934

[#955]: https://github.com/SynthstromAudible/DelugeFirmware/pull/955

[#963]: https://github.com/SynthstromAudible/DelugeFirmware/pull/963

[#966]: https://github.com/SynthstromAudible/DelugeFirmware/pull/966

[#970]: https://github.com/SynthstromAudible/DelugeFirmware/pull/970

[#976]: https://github.com/SynthstromAudible/DelugeFirmware/pull/976

[#991]: https://github.com/SynthstromAudible/DelugeFirmware/pull/991

[#994]: https://github.com/SynthstromAudible/DelugeFirmware/pull/994

[#1018]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1018

[#1039]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1039

[#1053]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1053

[#1065]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1065

[#1083]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1083

[#1114]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1114

[#1156]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1156

[#1159]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1159

[#1173]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1173

[#1183]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1183

[#1198]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1198

[#1251]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1251

[#1272]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1272

[#1305]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1305

[#1312]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1312

[#1344]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1344

[#1374]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1374

[#1382]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1382

[#1390]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1390

[#1456]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1456

[#1480]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1480

[#1506]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1506

[#1531]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1531

[#1542]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1542

[#1589]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1589

[#1607]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1607

[#1739]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1739

[#1824]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1824

[#1846]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1846

[#1898]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1898

[#1962]: https://github.com/SynthstromAudible/DelugeFirmware/pull/1962

[#2046]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2046

[#2080]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2080

[#2136]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2136

[#2166]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2166

[#2174]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2166

[#2260]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2260

[#2299]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2299

[#2315]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2315

[#2327]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2327

[#2330]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2330

[#2343]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2343

[#2345]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2345

[#2365]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2365

[#2367]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2367

[#2371]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2371

[#2376]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2376

[#2385]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2385

[#2421]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2421

[#2429]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2429

[#2475]: https://github.com/SynthstromAudible/DelugeFirmware/pull/2475

[Automation View Documentation]: features/automation_view.md

[Velocity View Documentation]: features/velocity_view.md

[Performance View Documentation]: features/performance_view.md

[MIDI Follow Mode Documentation]: features/midi_follow_mode.md

[MIDI Follow Mode Loopy Pro Template]: ../contrib/midi_follow/loopy_pro

[MIDI Follow Mode Touch OSC Template]: ../contrib/midi_follow/touch_osc

[DX7 Synth Documentation]: features/dx_synth.md

[Audio Export Documentation]: features/audio_export.md

[Chord Keyboard Documentation]: features/chord_keyboard.md
