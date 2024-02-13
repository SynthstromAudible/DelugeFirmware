# Deluge Menu Hierarchies

This document will document the menu structure hierarchy currently in place in the deluge in order to help users understand what exists in the menu structure and how the hierarchy strings are defined for OLED and 7SEG users.

The hierarchy's illustrated below will follow the format of OLED (7SEG) in order to show the Menu name on OLED and the Menu name on 7SEG in brackets.

# Settings menu

The Settings menu is accessible from anywhere on the Deluge by pressing `SHIFT + SELECT ENCODER`

The Settings menu contains the following menu hierarchy:

- CV
	- CV Output 1
		- Volts per Octave
		- Transpose
	- CV Output 2
		- Volts per Octave
		- Transpose
- Gate
	- Gate Output 1
		- V-Trig
		- S-Trig
	- Gate Output 2
		- V-Trig
		- S-Trig
	- Gate Output 3
		- V-Trig
		- S-Trig
	- Gate Output 4
		- V-Trig
		- S-Trig
	- Minimum Off-Time
- Trigger Clock
	- Input
		- PPQN
		- Auto-Start
			- Disabled
			- Enabled
	- Output
		- PPQN
- MIDI
	- Clock
		- Input
			- Disabled
			- Enabled
		- Output
			- Disabled
			- Enabled
		- Tempo Magnitude Matching
			- Disabled
			- Enabled
	- MIDI-Follow
		- Channel
			- Channel A
			- Channel B
			- Channel C
		- Kit Root Note
		- Feedback
			- Channel
			- Automation Feedback
				- Disabled
				- Low
				- Medium
				- High
			- Filter Responses
				- Disabled
				- Enabled
		- Display Param
			- Disabled
			- Enabled
	- Select Kit Row
		- Disabled
		- Enabled
	- MIDI-Thru
		- Disabled
		- Enabled
	- Takeover
		- Jump
		- Pickup
		- Scale
	- Commands
		- Play
		- Restart
		- Record
		- Tap Tempo
		- Undo
		- Redo
		- Loop
		- Layering Loop
		- Fill
	- Differentiate Inputs
		- Disabled
		- Enabled
	- Devices
		- DIN Ports
			- MPE
				- In
					- Lower Zone
					- Upper Zone
				- Out
					- Lower Zone
					- Upper Zone					
			- Velocity
			- Clock
				- Disabled
				- Enabled
		- Loopback
			- MPE
				- In
					- Lower Zone
					- Upper Zone
				- Out
					- Lower Zone
					- Upper Zone					
			- Velocity
			- Clock
				- Disabled
				- Enabled
- Defaults
	- UI
		- Song
			- Layout
				- Rows
				- Grid
			- Grid
				- Default Active Mode
					- Selection
					- Green
					- Blue
				- Select In Green Mode
					- Disabled
					- Enabled
				- Empty Pads
					- Unarm
					- Create + Record
		- Keyboard
			- Layout
				- Isomorphic
				- In-Key
	- Automation
		- Interpolation
			- Disabled
			- Enabled
		- Clear
			- Disabled
			- Enabled
		- Shift
			- Disabled
			- Enabled
		- Nudge Note
			- Disabled
			- Enabled
		- Disable Audition Pad Shortcuts
			- Disabled
			- Enabled
	- Tempo
	- Swing
	- Key
	- Scale
		- Major
		- Minor
		- Dorian
		- Phrygian
		- Lydian
		- Mixolydian
		- Locrian
		- Melodic Minor
		- Harmonic Minor
		- Hungarian Minor
		- Marva
		- Arabian
		- Whole Tone
		- Blues
		- Pentatonic Minor
		- Hirajoshi
		- Random
		- None
	- Velocity
	- Resolution
	- Bend Range
	- Metronome
- Swing Interval
	- 2-Bar
	- 1-Bar
	- 2nd-Notes
	- 4th-Notes
	- 8th-Notes
	- 16th-Notes
	- 32nd-Notes
	- 64th-Notes
	- 128th-Notes
	- 2-Bar-TPLTS
	- 1-Bar-TPLTS
	- 2nd-TPLTS
	- 4th-TPLTS
	- 8th-TPLTS
	- 16th-TPLTS
	- 32nd-TPLTS
	- 64th-TPLTS
	- 128th-TPLTS
	- 2-Bar-DTTED
	- 1-Bar-DTTED
	- 2nd-DTTED
	- 4th-DTTED
	- 8th-DTTED
	- 16th-DTTED
	- 32nd-DTTED
	- 64th-DTTED
	- 128th-DTTED
- Pads
	- Shortcut Version
		- 1.0
		- 3.0
	- Keyboard for Text
		- QWERTY
		- AZERTY
		- QWERTZ
	- Colours
		- Active
			- Red
			- Green
			- Blue
			- Yellow
			- Cyan
			- Purple
			- Amber
			- White
			- Pink
		- Stopped
			- Red
			- Green
			- Blue
			- Yellow
			- Cyan
			- Purple
			- Amber
			- White
			- Pink
		- Muted
			- Red
			- Green
			- Blue
			- Yellow
			- Cyan
			- Purple
			- Amber
			- White
			- Pink
		- Soloed 
			- Red
			- Green
			- Blue
			- Yellow
			- Cyan
			- Purple
			- Amber
			- White
			- Pink	
- Sample Preview
	- Disabled
	- Conditional
	- Enabled
- Play-Cursor
	- Fast
	- Disabled
	- Slow
- Recording
	- Count-in
		- Disabled
		- Enabled
	- Quantization
		- Off
		- 4-Bar
		- 2-Bar
		- 1-Bar
		- 2nd-Notes
		- 4th-Notes
		- 8th-Notes
		- 16th-Notes
		- 32nd-Notes
		- 64th-Notes
		- 128th-Notes
		- 256th-Notes
		- 512nd-Notes
		- 1024th-Notes
		- 2048th-Notes
		- 4096th-Notes
		- 8192nd-Notes
		- 16834th-Notes
		- 32768th-Notes
		- 65536th-Notes
		- 131072nd-Notes
		- 262144th-Notes
		- 524288th-Notes
		- 1048576th-Nptes
		- 2097152nd-Notes
		- 4194304th-Notes
		- 8388608th-Notes
		- 16777216th-Notes
	- Loop Margins
		- Disabled
		- Enabled
	- Sampling Monitoring
		- Conditional
		- Enabled
		- Disabled
- Community Features
	- Drum Randomizer
		- OFF
		- ON
	- Fine Tempo Knob
		- OFF
		- ON
	- Quantize
		- OFF
		- ON
	- Mod. Depth Decimals
		- OFF
		- ON
	- Catch Notes
		- OFF
		- ON
	- Delete Unused Kit Rows
		- OFF
		- ON
	- Alternative Golden Knob Delay Params
		- OFF
		- ON
	- Stutter Rate Quantize
		- OFF
		- ON
	- Allow Insecure Develop Sysex Messages
		- OFF
		- ON
	- Sync Scaling Action
		- Sync Scaling
		- Fill Mode
	- Highlight Incoming Notes
		- OFF
		- ON
	- Display Norns Layout
		- OFF
		- ON
	- Sticky Shift
		- OFF
		- ON
	- Light Shift
		- OFF
		- ON
	- Enable Grain FX
		- OFF
		- ON
	- Emulated Display
		- OLED
		- Toggle
		- 7SEG 
- Firmware Version

# Song Menu

The Song menu is accessible from Arranger View and Song View by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **

# Perform FX Menu

The Perform FX menu is accessible from Performance View by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **

# Sound Menu

The Sound menu is accessible from Synth Clips and Kit clips when affect entire is disabled and a kit row is selected by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **

# Kit FX Menu

The Kit FX menu is accessible from Kit clips when affect entire is enabled by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **

# MIDI Instrument Menu

The MIDI Instrument menu is accessible from MIDI clips by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **

# CV Instrument Menu

The CV Instrument menu is accessible from CV clips by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **

# Audio Clip Menu

The Audio Clip menu is accessible from Audio clips by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
