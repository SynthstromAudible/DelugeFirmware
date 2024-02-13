# Deluge Menu Hierarchies

This document will document the menu structure hierarchy currently in place in the deluge in order to help users understand what exists in the menu structure and how the hierarchy strings are defined for OLED and 7SEG users.

The hierarchy's illustrated below will follow the format of OLED (7SEG) in order to show the Menu name on OLED and the Menu name on 7SEG in brackets.

<details>
<summary>Settings menu</summary>

The Settings menu is accessible from anywhere on the Deluge by pressing `SHIFT + SELECT ENCODER`

The Settings menu contains the following menu hierarchy:

- CV
	- CV Output 1 (OUT1)
		- Volts per Octave (VOLT)
		- Transpose (TRAN)
	- CV Output 2 (OUT2)
		- Volts per Octave (VOLT)
		- Transpose (TRAN)
- Gate
	- Gate Output 1 (OUT1)
		- V-Trig (VTRI)
		- S-Trig (STRI)
	- Gate Output 2 (OUT2)
		- V-Trig (VTRI)
		- S-Trig (STRI)
	- Gate Output 3 (OUT3)
		- V-Trig (VTRI)
		- S-Trig (STRI)
	- Gate Output 4 (OUT4)
		- V-Trig (VTRI)
		- S-Trig (STRI)
	- Minimum Off-Time (OFFT)
- Trigger Clock (TCLO)
	- Input (IN)
		- PPQN 
		- Auto-Start (AUTO)
			- Disabled (ON)
			- Enabled (OFF)
	- Output (OUT)
		- PPQN
- MIDI
	- Clock (CLK)
		- Input (IN)
			- Disabled (ON)
			- Enabled (OFF)
		- Output (OUT)
			- Disabled (ON)
			- Enabled (OFF)
		- Tempo Magnitude Matching (MAGN)
			- Disabled
			- Enabled
	- MIDI-Follow (FOLO)
		- Channel (CHAN)
			- Channel A (A)
			- Channel B (B)
			- Channel C (C)
		- Kit Root Note (KIT)
		- Feedback (FEED)
			- Channel (CHAN)
			- Automation Feedback (AUTO)
				- Disabled (OFF)
				- Low
				- Medium (MEDI)
				- High
			- Filter Responses (FLTR)
				- Disabled (OFF)
				- Enabled (ON)
		- Display Param (DISP)
			- Disabled (ON)
			- Enabled (OFF)
	- Select Kit Row (KROW)
		- Disabled (OFF)
		- Enabled (ON)
	- MIDI-Thru (THRU)
		- Disabled (OFF)
		- Enabled (ON)
	- Takeover (TOVR)
		- Jump
		- Pickup (PICK)
		- Scale (SCAL)
	- Commands (CMD)
		- Play
		- Restart (REST)
		- Record (REC)
		- Tap Tempo (TAP)
		- Undo
		- Redo
		- Loop
		- Layering Loop (LAYE)
		- Fill
	- Differentiate Inputs (DIFF)
		- Disabled (OFF)
		- Enabled (ON)
	- Devices (DEVI)
		- DIN Ports (DIN)
			- MPE
				- In
					- Lower Zone (LOWE)
					- Upper Zone (UPPE)
				- Out
					- Lower Zone (LOWE)
					- Upper Zone (UPPE)				
			- Velocity (VELO)
			- Clock (CLK)
				- Disabled (OFF)
				- Enabled (ON)
		- Loopback
			- MPE
				- In
					- Lower Zone (LOWE)
					- Upper Zone (UPPE)
				- Out
					- Lower Zone (LOWE)
					- Upper Zone (UPPE)				
			- Velocity (VELO)
			- Clock (CLK)
				- Disabled (OFF)
				- Enabled (ON)
- Defaults (DEFA)
	- UI
		- Song
			- Layout (LAYT)
				- Rows
				- Grid
			- Grid
				- Default Active Mode (DEFM)
					- Selection (SELE)
					- Green (GREE)
					- Blue
				- Select In Green Mode (GREE)
					- Disabled (OFF)
					- Enabled (ON)
				- Empty Pads (EMPT)
					- Unarm (UNAR)
					- Create + Record (CREA)
		- Keyboard (KEY)
			- Layout (LAYT)
				- Isomorphic (ISO)
				- In-Key (INKY)
	- Automation (AUTO)
		- Interpolation (INTE)
			- Disabled (OFF)
			- Enabled (ON)
		- Clear (CLEA)
			- Disabled (OFF)
			- Enabled (ON)
		- Shift (SHIF)
			- Disabled (Off)
			- Enabled (ON)
		- Nudge Note (NUDG)
			- Disabled (OFF)
			- Enabled (ON)
		- Disable Audition Pad Shortcuts (SCUT)
			- Disabled (OFF)
			- Enabled (ON)
	- Tempo (TEMP)
	- Swing (SWIN)
	- Key
	- Scale (SCAL)
		- Major (MAJO)
		- Minor (MINO)
		- Dorian (DORI)
		- Phrygian (PHRY)
		- Lydian (LYDI)
		- Mixolydian (MIXO)
		- Locrian (LOCR)
		- Melodic Minor (MELO)
		- Harmonic Minor (HARM)
		- Hungarian Minor (HUNG)
		- Marva (MARV)
		- Arabian (ARAB)
		- Whole Tone (WHOL)
		- Blues (BLUE)
		- Pentatonic Minor (PENT)
		- Hirajoshi (HIRA)
		- Random (RAND)
		- None
	- Velocity (VELO)
	- Resolution (RESO)
	- Bend Range (BEND)
	- Metronome (METR)
- Swing Interval (SWIN) - NOTE: These options can change depending on how your default resolution is set
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
	- Shortcut Version (SHOR)
		- 1.0
		- 3.0
	- Keyboard for Text (KEYB)
		- QWERTY (QWER)
		- AZERTY (AZER)
		- QWERTZ (QRTZ)
	- Colours (COLO)
		- Active (ACTI)
			- Red
			- Green (GREEN)
			- Blue
			- Yellow (YELL)
			- Cyan
			- Purple (PURP)
			- Amber (AMBE)
			- White (WHIT)
			- Pink
		- Stopped (STOP)
			- Red
			- Green (GREE)
			- Blue
			- Yellow (YELL)
			- Cyan
			- Purple (PURP)
			- Amber (AMBE)
			- White (WHIT)
			- Pink
		- Muted (MUTE)
			- Red
			- Green (GREE)
			- Blue
			- Yellow (YELL)
			- Cyan
			- Purple (PURP)
			- Amber (AMBE)
			- White (WHIT)
			- Pink
		- Soloed 
			- Red
			- Green (GREE)
			- Blue
			- Yellow (YELL)
			- Cyan
			- Purple (PURP)
			- Amber (AMBE)
			- White (WHIT)
			- Pink
- Sample Preview (PREV)
	- Disabled (OFF)
	- Conditional (COND)
	- Enabled (ON)
- Play-Cursor (CURS)
	- Fast
	- Disabled (OFF)
	- Slow
- Recording (RECO)
	- Count-in (COUN)
		- Disabled (OFF)
		- Enabled (ON)
	- Quantization (QUAN)
		- Off
		- 4-Bar (4BAR)
		- 2-Bar (2BAR)
		- 1-Bar (1BAR)
		- 2nd-Notes (2ND)
		- 4th-Notes (4TH)
		- 8th-Notes (8TH)
		- 16th-Notes (16TH)
		- 32nd-Notes (32ND)
		- 64th-Notes (64th)
		- 128th-Notes (128T)
		- 256th-Notes (256T)
		- 512nd-Notes (512T)
		- 1024th-Notes (1024)
		- 2048th-Notes (2048)
		- 4096th-Notes (4096)
		- 8192nd-Notes (8192)
		- 16834th-Notes (TINY)
		- 32768th-Notes (TINY)
		- 65536th-Notes (TINY)
		- 131072nd-Notes (TINY)
		- 262144th-Notes (TINY)
		- 524288th-Notes (TINY)
		- 1048576th-Nptes (TINY)
		- 2097152nd-Notes (TINY)
		- 4194304th-Notes (TINY)
		- 8388608th-Notes (TINY)
		- 16777216th-Notes (TINY)
	- Loop Margins (MARG)
		- Disabled (OFF)
		- Enabled (ON)
	- Sampling Monitoring (MONI)
		- Conditional (COND)
		- Enabled (ON)
		- Disabled (OFF)
- Community Features (FEAT)
	- Drum Randomizer (DRUM)
		- OFF
		- ON
	- Fine Tempo Knob (TEMP)
		- OFF
		- ON
	- Quantize (QUAN)
		- OFF
		- ON
	- Mod. Depth Decimals (MOD.)
		- OFF
		- ON
	- Catch Notes (CATC)
		- OFF
		- ON
	- Delete Unused Kit Rows (UNUS)
		- OFF
		- ON
	- Alternative Golden Knob Delay Params (DELA)
		- OFF
		- ON
	- Stutter Rate Quantize (STUT)
		- OFF
		- ON
	- Allow Insecure Develop Sysex Messages (SYSX)
		- OFF
		- ON
	- Sync Scaling Action (SCAL)
		- Sync Scaling (SCAL)
		- Fill Mode (FILL)
	- Highlight Incoming Notes (HIGH)
		- OFF
		- ON
	- Display Norns Layout (NORN)
		- OFF
		- ON
	- Sticky Shift (STIC)
		- OFF
		- ON
	- Light Shift (LIGH)
		- OFF
		- ON
	- Enable Grain FX (GRFX)
		- OFF
		- ON
	- Emulated Display (EMUL)
		- OLED (OLED)
		- Toggle (TOGL)
		- 7SEG (7SEG)
- Firmware Version (FIRM)
</details>

<details>
<summary>Song Menu</summary>

The Song menu is accessible from Arranger View and Song View by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>

<details>
<summary>Perform FX Menu</summary>

The Perform FX menu is accessible from Performance View by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>

<details>
<summary>Sound Menu</summary>

The Sound menu is accessible from Synth Clips and Kit clips when affect entire is disabled and a kit row is selected by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>

<details>
<summary>Kit FX Menu</summary>

The Kit FX menu is accessible from Kit clips when affect entire is enabled by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>

<details>
<summary>MIDI Instrument Menu</summary>

The MIDI Instrument menu is accessible from MIDI clips by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>

<details>
<summary>CV Instrument Menu</summary>

The CV Instrument menu is accessible from CV clips by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>

<details>
<summary>Audio Clip Menu</summary>

The Audio Clip menu is accessible from Audio clips by pressing on the `SELECT ENCODER`

** Menu hierarchy to be added **
</details>


