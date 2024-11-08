# Deluge Community Firmware Github Directory Reference
- [Deluge Community Firmware Github Directory Reference](#deluge-community-firmware-github-directory-reference)
- [Main Folder](#main-folder)
- [Source Folder](#source-folder)
- [Deluge Folder](#deluge-folder)
  - [GUI Folder](#gui-folder)
    - [UI Folder](#ui-folder)
    - [Views Folder](#views-folder)
  - [Model Folder](#model-folder)

# Main Folder
`DelugeFirmware/`

*Contains the main README.md file and several files and directories related to managing the repository and building the software.* 

Important folders:
| Folder | Contents
|-|-
| docs | Contains documentation for using the Deluge
| pages/public | Contains the public website for the community firmware
| [src](#source-folder) | Contains the source code

# Source Folder 
`DelugeFirmware/src/`

*Contains all source code. Most functional code lives in subfolder [`deluge/`](#deluge-folder). Everything else is managing low-level systems, global definitions, etc.*

# Deluge Folder
`DelugeFirmware/src/deluge`

*Main directory for firmware code. `deluge.cpp` runs the boot sequence and main loop*

| Folder | Contents
|-|-
| drivers | mostly deals with managing hardware
| dsp | digital signal processing, manages audio effects
| [gui](#gui-folder) | graphical user interface, manages all user-facing behavior of the Deluge
| hid | human interface device, manages basic functionality of knobs, buttons, pads, LEDs, display
| io | input/output, manages debug-logging and incoming midi
| memory | memory management
| [model](#model-folder) | manages model_stack, the model Deluge uses to represent and pass around its internal state
| modulation | manages internal modulating of parameters, including automation and midi
| playback | manages playback and recording modes
| processing | manages various processes: audio, CV and live audio engines, sound instruments, metronome, stem export
| storage | manages using files from the SD card
| testing | automated tests
| util | collection of general utilities
| version | deals with the code version

## GUI Folder
`DelugeFirmware/src/deluge/gui`

*Graphical User Interface. This folder manages all user-facing behavior of the Deluge and is therefor the main focus for fixing bugs and developing new features*

| Folder | Contents
|-|-
| colour | colour definitions
| context_menu | the context menu pops up on top of the regular menu, depending on contextual actions
| fonts | font definitions
| l10n | list of strings used in the ui for both 7SEG and oled, can be used for localization
| menu_item | definitions for the options of all menus
| ui | manages the foundational layer of the ui including menus, browsers, keyboard views and saving/loading. `ui.cpp` serves as a base-class that the different views/modes of the Deluge are built on
| [views](#views-folder) | manages the main views/modes of the Deluge, such as clip view and arranger view
| waveform | manages the waveform view

### UI Folder
`DelugeFirmware/src/deluge/gui/ui/`

*User Interface. Many low-level and base methods of the user interface are defined here, and reused in higher level code*

| Folder | Contents
|-|-
| browser | `Browser` is a `QwertyUI` which handles browsing files on the SD card. It has sub-classes for browsing DX7-presets (`DxSyxBrowser`,) audio samples (`SampleBrowser`,) and slots for instrument clips and songs (`SlotBrowser`)
| keyboard | manages the keyboard overlay
| load | `LoadUI` is a `SlotBrowser` with sub-classes `LoadInstrumentPresetUI`, `LoadMidiDeviceDefinitionUI` and `LoadSongUI`
| rename | `RenameUI` is a `QwertyUI` with sub-classes `RenameClipUI`, `RenameDrumUI`, `RenameMidiCCUI` and `RenameOutputUI`
| save | `SaveUI` is a `SlotBrowser` with sub-classes `SaveInstrumentPresetUI`, `SaveKitRowUI`, `SaveMidiDeviceDefinitionUI` and `SaveSongUI`

| Class | Relevance
|-|-
| `AudioRecorder` | is a `UI`
| menus | Handles the main menu system. There's no overarching class here
| `QwertyUI` | is a `UI` that uses the grid pads as a qwerty keyboard
| `RootUI` | is a `UI` that basically only implements a list of virtual methods for sub-classes
| `SampleMarkerEditor` | is a `UI` for editing sample start/stop/loop points
| `Slicer` | is a `UI` for slicing samples
| `SoundEditor` | is a `UI` *(it looks like this UI opens when the Sound Menu is opened or a sound parameter is selected with shift+pad? I'm not exactly sure)*
| `UI` | Absolute base class of all UIs

### Views Folder
`DelugeFirmware/src/deluge/gui/views`

*This folder manages all the main modes of the Deluge ui*

Each of the `final` views - that is, each view that is not meant to be a base-class for something else - defines a global variable of itself at the bottom of its header file. For example:
```cpp
extern AutomationView automationView;
```
This global variable is reused anytime that view is activated.

| View | Info
|-|-
| `ArrangerView final` | is a `TimelineView` representing the arranger view
| `AudioClipView final` | is a `ClipView` for audio clips
| `AutomationView final` | is a `ClipView` that handles the automation view in different contexts
| `ClipNavigationTimelineView` | is a `TimelineView`
| `ClipView` | is a `ClipNavigationTimelineView` set up to handle a single clip
| `InstrumentClipView final` | is a `ClipView` for instrument clips
| `PerformanceSessionView final` | is a `ClipNavigationTimelineView` that implements performance fx view
| `SessionView final` | is a `ClipNavigationTimelineView`. This is *Song View* and contains both *Song Row View* and *Song Grid View*
| `TimelineView` | inherits from `RootUI` and `UI`, and is itself a base class for all other views
| `View` | defines a global variable `view` which represents a layer of the user interface which is always available, regardless of what specific view we're in

## Model Folder
`DelugeFirmware/src/deluge/model`
