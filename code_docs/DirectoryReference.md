# Deluge Community Firmware Github Directory Reference
- [Deluge Community Firmware Github Directory Reference](#deluge-community-firmware-github-directory-reference)
- [Main Folder](#main-folder)
- [Source Folder](#source-folder)
- [Deluge Folder](#deluge-folder)
  - [GUI Folder](#gui-folder)
    - [Views Folder](#views-folder)

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
| model | manages model_stack, the model Deluge uses to represent and pass around its internal state
| modulation | manages internal modulating of parameters, including automation and midi
| playback | manages playback and recording modes
| processing | manages various processes: audio, CV and live audio engines, sound instruments, metronome, stem export
| storage | manages using files from the SD card
| testing | automated tests
| util | collection of general utilities
| version | deals with the code version

## GUI Folder
`DelugeFirmware/src/deluge/gui`

*Graphical User Interface. This folder manages all user-facing behavior of the Deluge and is therefor the main focus-point for fixing bugs and developing new features*

| Folder | Contents
|-|-
| colour | colour definitions
| context_menu | the context menu pops up on top of the regular menu, depending on contextual actions
| fonts | font definitions
| l10n | list of strings used in the ui for both 7SEG and oled, can be used for localization
| menu_item | definitions for the options of all menus
| ui | manages the foundational layer of the ui including menus, browsers, keyboard views and saving/loading. `ui.cpp` serves as a base-class that the different views/modes of the Deluge are built on
| views | manages the main views/modes of the Deluge, such as clip view and arranger view
| waveform | manages the waveform view

### Views Folder
`DelugeFirmware/src/deluge/gui`
