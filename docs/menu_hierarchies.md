# Deluge Menu Hierarchies

This document will document the menu structure hierarchy currently in place in the deluge in order to help users understand what exists in the menu structure and how the hierarchy strings are defined for OLED and 7SEG users.

The hierarchy's illustrated below will follow the format of OLED (7SEG) in order to show the Menu name on OLED and the Menu name on 7SEG in brackets.

<details>
<summary>Settings Menu</summary>

The Settings menu is accessible from anywhere on the Deluge by pressing `SHIFT + SELECT ENCODER`

The Settings menu contains the following menu hierarchy:

<blockquote>
<details><summary>CV</summary>

	- CV Output 1 (OUT1)
		- Volts per Octave (VOLT)
		- Transpose (TRAN)
	- CV Output 2 (OUT2)
		- Volts per Octave (VOLT)
		- Transpose (TRAN)
</details>
<details><summary>Gate</summary>

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
</details>

<details><summary>Trigger Clock (TCLO)</summary>

	- Input (IN)
		- PPQN
		- Auto-Start (AUTO)
			- Disabled (ON)
			- Enabled (OFF)
	- Output (OUT)
		- PPQN
</details>

<details><summary>MIDI</summary>

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
			- Disabled (OFF)
			- Enabled (ON)
		- Control Song Param (SONG)
			- Disabled (OFF)
			- Enabled (ON)
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
		- Relative (RELA)
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
		- Transpose (TRAN)
		- Load Next Song (SONG)
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
</details>

<details><summary>Defaults (DEFA)</summary>

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
				- Piano (PIAN)
			- Sidebar Controls (CTRL)
				- Momentary Velocity (MVEL)
					- Disabled (OFF)
					- Enabled (ON)
				- Momentary Modwheel (MMOD)
					- Disabled (OFF)
					- Enabled (ON)
			- Favourites (FAV)
				- Favourites Only (16)
				- Favourites and Banks (256)
				- Off (OFF)
		- Clip Type (CLIP)
			- New Clip Type (TYPE)
				- Synth
				- Kit
				- Midi
				- CV
				- Audio
			- Use Last Clip Type (LAST)
				- Disabled (OFF)
				- Enabled (ON)
		- Horizontal Menu (HORZ)
			- Alternative Select Behaviour (SELE)
				- Disabled (OFF)
				- Enabled (ON)
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
	- Swing Amount (SWIA)
	- Swing Interval (SWII)
	- Key
	- Scale (SCAL)
		- Init Scale
		- Active Scales
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
	- Startup Song (SONG)
		- New Song (NEW)
		- Template Song (Default.XML) (TMPL)
		- Last Opened Song (OPEN)
		- Last Saved Song (SAVE)
	- Pad Brightness
	- Sample Slice Mode
		- Cut
		- Once
		- Loop
		- Stretch
	- High CPU Indicator (CPU)
		- Disabled (OFF)
		- Enabled (ON)
	- Hold Press Time (HOLD)
</details>

<details><summary>Swing Interval (SWII)</summary>
NOTE: These options can change depending on how your default resolution is set

	- 2-Bar
	- 1-Bar
	- 2nd-Notes
	- 4th-Notes
	- 8th-Notes
	- 16th-Notes
	- 32nd-Notes
	- 64th-Notes
	- 128th-Notes
</details>

<details><summary>Pads</summary>

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
</details>

<details><summary>Sample Preview (PREV)</summary>

	- Disabled (OFF)
	- Conditional (COND)
	- Enabled (ON)
</details>

<details><summary>Play-Cursor (CURS)</summary>

	- Fast
	- Disabled (OFF)
	- Slow
</details>

<details><summary>Recording (RECO)</summary>

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
		- 64th-Notes (64TH)
	- Loop Margins (MARG)
		- Disabled (OFF)
		- Enabled (ON)
	- Sampling Monitoring (MONI)
		- Conditional (COND)
		- Enabled (ON)
		- Disabled (OFF)
	- Threshold Recording (THRE)
		- Mode
			- Disabled (OFF)
			- Low
			- Medium
			- High
	- Loop Command (LOOP)
		- Loop
		- Layering Loop
</details>

<details><summary>Community Features (FEAT)</summary>

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
  	- Reverse Stutter Rate (RSTUT)
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
	- Grain FX (GRFX)
		- OFF
		- ON
	- DX Shortcuts (DX7S)
		- OFF
		- ON
	- Emulated Display (EMUL)
		- OLED (OLED)
		- Toggle (TOGL)
		- 7SEG (7SEG)
	- KB View Sidebar Menu Exit (EXIT)
		- OFF
		- ON
	- Launch Event Playhead (PLAY)
		- OFF
		- ON
	- Chord Keyboards (CHRD)
		- OFF
		- ON
	- Alternative Playback Start Behaviour (STAR)
		- OFF
		- ON
	- Accessibility Shortcuts (ACCE)
		- OFF
		- ON
	- Grid View Loop Pads (LOOP)
		- OFF
		- ON
	- Trim from start of audio clips (TRIM)
		- OFF
		- ON
</details>

Firmware Version (FIRM)

</details>

<details>
<summary>Song Clip Settings Menu</summary>

The Clip Settings menu is accessible from `Song Grid View` by pressing and holding a `CLIP` and pressing the `SELECT ENCODER`.

The Clip Settings menu is accessible from `Song Row View` by pressing and holding the `CLIP MUTE` pad in the first column of the sidebar and pressing the `SELECT ENCODER`.

The Clip settings menu contains the following menu hierarchy:

<blockquote>
<details><summary>Convert to Audio (CONV)</summary></details>
<details><summary>Clip Mode (MODE)</summary>

	- INFINITE (INF)
	- FILL
	- ONCE
</details>
<details><summary>Clip Name (NAME)</summary></details>
</details>

<details>
<summary>Song Menu</summary>

The Song menu is accessible from `Arranger View` and `Song View` by pressing on the `SELECT ENCODER`

The Song menu contains the following menu hierarchy:

<blockquote>
<details><summary>Master (MASTR)</summary>

	- Volume (VOLU)
	- Pan
</details>
<details><summary>Filters (FLTR)</summary>

	- LPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Morph (MORP)
		- Mode (MODE)
			- 12DB Ladder (LA12)
			- 24DB Ladder (LA24)
			- Drive (DRIV)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
	- HPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Morph (MORP)
		- Mode (MODE)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
			- HP Ladder (HP_L)
	- Filter Route (ROUT)
		- HPF2LPF (HPF2)
		- LPF2HPF (LPF2)
		- PARALLEL (PARA)
</details>
<details><summary>FX</summary>

	- EQ
		- Bass
		- Treble (TREB)
		- Bass Frequency (BAFR)
		- Treble Frequency (TRFR)
	- Delay (DELA)
		- Amount (AMOU)
		- Rate
		- Pingpong (PING)
			- Disabled (OFF)
			- Enabled (ON)
		- Type
			- Digital (DIGI)
			- Analog (ANA)
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
	- Reverb (REVE)
		- Amount (AMOU)
  			- Freeverb (FVRB)
          		- Mutable (MTBL)
		- Model (MODE)
		- Room Size (SIZE) (if Freeverb is Selected) or Time (if Mutable is Selected)
		- Damping (DAMP)
		- Width (WIDT) (if Freeverb is Selected) or Diffusion (DIFF) (if Mutable is Selected)
  		- HPF (if Mutable is Selected)
		- Pan
		- Reverb Sidechain (SIDE)
			- Volume Ducking (VOLU)
	
	- Stutter (STUT)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Stutter (STUT)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Mod-FX (MODU)
		- Type
			- Disabled (OFF)
			- Flanger (FLAN)
			- Chorus (CHOR)
			- Phaser (PHAS)
			- Stereo Chorus (S.CHO)
			- Warble (WARB)
			- Grain (GRAI)
		- Rate
		- Depth (DEPT) (if Chorus, Phaser, Warble or Grain is selected)
			- Displayed as Grain Amount (Amnt) if Grain is selected
		- Feedback (FEED) (if Flanger, Phaser, Warble or Grain is selected)
			- Displayed as Grain Type (GRTY) if Grain is selected
		- Offset (OFFS) (if Chorus, Warble or Grain is selected)
			- Displayed as Grain Size (SIZE) if Grain is selected
	- Distortion (DIST)
		- Decimation (DECI)
		- Bitcrush (CRUS)
</details>
<details><summary>Swing Interval (SWII)</summary></details>
<details><summary>Active Scales</summary>
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
</details>
<details><summary>Configure Macros (MACR)</summary></details>
<details><summary>Midi Learn (MIDI)</summary></details>
<details><summary>Audio Export (EXPO)</summary>

	- Start Export (EXPO)
	- Configure Export (CONF)
		- Normalization (NORM)
			- Disabled (OFF)
			- Enabled (ON)
		- Export to Silence (SILE)
			- Disabled (OFF)
			- Enabled (ON)
		- Song FX (SONG)
			- Disabled (OFF)
			- Enabled (ON)
		- Offline Rendering (OFFR)
			- Disabled (OFF)
			- Enabled (ON)
		- Export Mixdown (MIXD)
			- Disabled (OFF)
			- Enabled (ON)
</details>
</details>

<details>
<summary>Perform FX Menu</summary>

The Perform FX menu is accessible from Performance View by pressing on the `SELECT ENCODER`

The Perform FX menu contains the following menu hierarchy:

<blockquote>
<details><summary>Editing Mode (EDIT)</summary>

	- Disabled (OFF)
	- Value
	- Param
</details>
<details><summary>Filters (FLTR)</summary>

	- See Song menu hierarchy above for break-down of Filters menu
</details>
<details><summary>FX</summary>

	- See Song menu hierarchy above for break-down of FX menu
</details>

</details>

<details>
<summary>Sound Menu</summary>

The Sound menu is accessible from Synth Clips and Kit clips when affect entire is disabled and a kit row is selected by pressing on the `SELECT ENCODER`

The Sound menu contains the following menu hierarchy:

<blockquote>
<details><summary>Master (MASTR)</summary>

	- Volume (VOLU)
	- Master Transpose (TRAN)
	- Vibrato (VIBR)
	- Pan
	- Synth Mode (MODE) - in Synth's and Kit row's that have loaded a Synth preset
		- Subtractive
		- FM
		- Ringmod
	- Name - in Kit's only for naming a Kit row
</details>
<details><summary>Arpeggiator (ARPE)</summary>

	- Enabled (ON)
	- Basic
		- Gate
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
		- Rate
	- Pattern
		- Octaves (OCTA)
		- Octave Mode (OMOD)
			- Up
			- Down
			- Up & Down (UPDN)
			- Alternate (ALT)
			- Random (RAND)
		- Chord Simulator (CSIM) (NOTE: Available only in Kit Rows)
		- Note Mode (NMOD)
			- Up
			- Down
			- Up & Down (UPDN)
			- Played Order (PLAY)
			- Random (RAND)
		- Step Repeat (REPE)
		- Rhythm (RHYT)
		- Sequence Length (LENG)
	- Randomizer
		- Lock Randomizer (LOCK)
		- Octave Spread (OCTA)
		- Gate Spread (GATE)
		- Velocity Spread (VELO)
		- Ratchet Amount (RATC)
		- Ratchet Probability (RPRO)
		- Chord Polyphony (CHRD) (NOTE: NOT available for Sound Kit Rows)
		- Chord Probability (CPRO) (NOTE: NOT available for Sound Kit Rows)
    	- Note Probability (NOTE)
    	- Swap Probability (SWAP)
    	- Bass Probability (BASS)
    	- Glide Probability (GLID)
    	- Reverse Probability (RVRS)
	- MPE
		- Velocity (VELO)
			- Disabled (OFF)
			- Aftertouch
			- MPE Y (Y)
	- Include in Kit Arp (KARP) (NOTE: available only for Kit Rows)
</details>

<details><summary>Randomizer (RAND)</summary>

	- Velocity Spread (VELO)
	- Note Probability (NOTE)
	- Reverse Probability (RVRS)
</details>
<details><summary>Compressor (COMP)</summary>

	- Threshold (THRE)
	- Ratio (RATI)
	- Attack (ATTA)
	- Release (RELE)
	- HPF
</details>
<details><summary>Filters (FLTR)</summary>

	- LPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Mode (MODE)
			- 12DB Ladder (LA12)
			- 24DB Ladder (LA24)
			- Drive (DRIV)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
		- Drive (DRIV) (if 12DB/24DB/Drive mode is selected) or Morph (MORP) (if SVF mode is selected)
	- HPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Mode (MODE)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
			- HP Ladder (HP_L)
		- Morph (MORP) (if SVF mode is selected) or FM (if HP Ladder mode is selected)
	- Filter Route (ROUT)
		- HPF2LPF (HPF2)
		- LPF2HPF (LPF2)
		- PARALLEL (PARA)
</details>
<details><summary>FX</summary>

	- EQ
		- Bass
		- Treble (TREB)
		- Bass Frequency (BAFR)
		- Treble Frequency (TRFR)
	- Delay (DELA)
		- Amount (AMOU)
		- Rate
		- Pingpong (PING)
			- Disabled (OFF)
			- Enabled (ON)
		- Type
			- Digital (DIGI)
			- Analog (ANA)
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
	- Reverb (REVE)
		- Amount (AMOU)
  			- Freeverb (FVRB)
          		- Mutable (MTBL)
		- Model (MODE)
		- Room Size (SIZE) (if Freeverb is Selected) or Time (if Mutable is Selected)
		- Damping (DAMP)
		- Width (WIDT) (if Freeverb is Selected) or Diffusion (DIFF) (if Mutable is Selected)
  		- HPF (if Mutable is Selected)
		- Pan
		- Reverb Sidechain (SIDE)
			- Volume Ducking (VOLU)
	
	- Stutter (STUT)
		- Use Song Settings (SONG)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Stutter (STUT)
		- Use Song Settings (SONG)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Mod-FX (MODU)
		- Type
			- Disabled (OFF)
			- Flanger (FLAN)
			- Chorus (CHOR)
			- Phaser (PHAS)
			- Stereo Chorus (S.CHO)
			- Warble (WARB)
			- Grain (GRAI)
		- Rate
		- Depth (DEPT) (if Chorus, Phaser, Warble or Grain is selected)
			- Displayed as Grain Amount (Amnt) if Grain is selected
		- Feedback (FEED) (if Flanger, Phaser, Warble or Grain is selected)
			- Displayed as Grain Type (GRTY) if Grain is selected
		- Offset (OFFS) (if Chorus, Warble or Grain is selected)
			- Displayed as Grain Size (SIZE) if Grain is selected
	- Distortion (DIST)
		- Saturation (SATU)
		- Decimation (DECI)
		- Bitcrush (CRUS)
		- Wavefold (FOLD)
	- Noise Level (NOIS)
</details>
<details><summary>Sidechain (SIDE) </summary>

	- Volume Ducking (VOLU)
	- Sync
	NOTE: These options can change depending on how your default resolution is set

		- Off
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
	- Attack (ATTA)
	- Release (RELE)
	- Shape (TYPE)
</details>
<details><summary>Oscillator 1 (OSC1) </summary>

	- Type
		- Sine
		- Triangle (TRIA)
		- Square (SQUA)
		- Analog Square (ASQUARE)
		- Saw
		- Analog Saw (ASAW)
		- Wavetable
		- Sample (SAMP)
		- Input (IN)

	- Volume (VOLU)
	- Wave-Index (WAVE) - if Wavetable type is selected
	- File Browser (FILE) - if Wavetable or Sample type is selected
	- Record Audio (RECO)
	- Reverse (REVE) - if Sample type is selected
		- Disabled (OFF)
		- Enabled (ON)
	- Repeat Mode (MODE)
		- Cut
		- Once
		- Loop
		- Stretch
	- Start-Point (STAR) - if Sample type is selected
	- End-Point (END-) - if Sample type is selected
	- Transpose (TRAN)
	- Pitch/Speed (PISP)
		- Linked
		- Independent
	- Interpolation (INTE) - if Input type is selected
		- Linear
		- Sync
	- Speed (SPEE) - if Sample type selected
	- Pulse Width (PULS) - if any type except Sample or Input is selected
	- Retrigger Phase (RETR) - if any type except Sample is selected
</details>
<details><summary>Oscillator 2 (OSC2) </summary>

	- Type
		- Sine
		- Triangle (TRIA)
		- Square (SQUA)
		- Analog Square (ASQUARE)
		- Saw
		- Analog Saw (ASAW)
		- Wavetable
		- Sample (SAMP)
		- Input (IN)

	- Volume (VOLU)
	- Wave-Index (WAVE) - if Wavetable type is selected
	- File Browser (FILE) - if Wavetable or Sample type is selected
	- Record Audio (RECO)
	- Reverse (REVE) - if Sample type is selected
		- Disabled (OFF)
		- Enabled (ON)
	- Repeat Mode (MODE)
		- Cut
		- Once
		- Loop
		- Stretch
	- Start-Point (STAR) - if Sample type is selected
	- End-Point (END-) - if Sample type is selected
	- Transpose (TRAN)
	- Pitch/Speed (PISP)
		- Linked
		- Independent
	- Interpolation (INTE) - if Input type is selected
		- Linear
		- Sync
	- Speed (SPEE) - if Sample type selected
	- Pulse Width (PULS) - if any type except Sample or Input is selected
	- Oscillator Sync (SYNC)
		- Disabled (OFF)
		- Enabled (ON)
	- Retrigger Phase (RETR) - if any type except Sample is selected
</details>
<details><summary>Envelope 1 (ENV1) </summary>

	- Attack (ATTA)
	- Decay (DECA)
	- Sustain (SUST)
	- Release (RELE)
</details>
<details><summary>Envelope 2 (ENV2) </summary>

	- Attack (ATTA)
	- Decay (DECA)
	- Sustain (SUST)
	- Release (RELE)
</details>
<details><summary>LFO1 </summary>

	- Shape (TYPE)
		- Sine
		- Triangle (TRIA)
		- Square (SQUA)
		- Saw
		- S&H (S H)
		- Random Walk (RWLK)
	- Sync
	NOTE: These options can change depending on how your default resolution is set

		- Off
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
	- Rate
</details>
<details><summary>LFO2 </summary>

	- Shape (TYPE)
		- Sine
		- Triangle (TRIA)
		- Square (SQUA)
		- Saw
		- S&H (S H)
		- Random Walk (RWLK)
	- Sync
	NOTE: These options can change depending on how your default resolution is set

		- Off
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
	- Rate
</details>
<details><summary>Voice (VOIC) </summary>

	- Polyphony Type (POLY)
		- Auto (Can play chords, but starting a new note ends any releasing ones)
		- Polyphonic (Can play up to MAX VOICES notes. Click for MAX VOICES sub menu to set number of voices)
		- Monophonic (Each note stops all other notes, retriggers envelope)
		- Legato (Each note stops all other notes, does not retrigger envelope)
	- Unison (UNIS)
		- Unison Number (NUM)
		- Unison Detune (DETU)
		- Unison Stereo Spread (SPRE)
    - Max Voices (VCNT)
	- Portamento (PORT)
	- Priority (PRIO)
		- Low
		- Medium
		- High
</details>
<details><summary>Bend Range (BEND) </summary>

	- Normal (NORM)
	- Poly / Finger / MPE (MPE)
</details>
<details><summary>Mod Matrix (MMTR) </summary></details>
<details><summary>Play Direction (DIRE) </summary>

	- Forward
	- Reversed
	- Ping-Pong
</details>
<details><summary>MIDI</summary>

	- Channel
	- Note (only available for Kits)
</details>

</details>

<details>
<summary>Kit FX Menu</summary>

The Kit FX menu is accessible from Kit clips when affect entire is enabled by pressing on the `SELECT ENCODER`

The Kit FX menu contains the following menu hierarchy:

<blockquote>
<details><summary>Master (MASTR)</summary>

	- Volume (VOLU)
	- Pitch (PITC)
	- Pan
</details>
<details><summary>Kit Arpeggiator (KARP)</summary>

	- Enabled (ON)
	- Basic
		- Gate
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
		- Rate
	- Pattern
		- Note Mode (NMOD)
			- Up
			- Down
			- Up & Down (UPDN)
			- Played Order (PLAY)
			- Random (RAND)
		- Step Repeat (REPE)
		- Rhythm (RHYT)
		- Sequence Length (LENG)
	- Randomizer
		- Lock Randomizer (LOCK)
		- Gate Spread (GATE)
		- Velocity Spread (VELO)
		- Ratchet Amount (RATC)
		- Ratchet Probability (RPRO)
    	- Note Probability (NOTE)
    	- Swap Probability (SWAP)
    	- Bass Probability (BASS)
    	- Glide Probability (GLID)
    	- Reverse Probability (RVRS)
</details>
<details><summary>Compressor (COMP)</summary>

	- Threshold (THRE)
	- Ratio (RATI)
	- Attack (ATTA)
	- Release (RELE)
	- HPF
</details>
<details><summary>Filters (FLTR)</summary>

	- LPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Morph (MORP)
		- Mode (MODE)
			- 12DB Ladder (LA12)
			- 24DB Ladder (LA24)
			- Drive (DRIV)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
	- HPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Morph (MORP)
		- Mode (MODE)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
			- HP Ladder (HP_L)
	- Filter Route (ROUT)
		- HPF2LPF (HPF2)
		- LPF2HPF (LPF2)
		- PARALLEL (PARA)
</details>
<details><summary>FX</summary>

	- EQ
		- Bass
		- Treble (TREB)
		- Bass Frequency (BAFR)
		- Treble Frequency (TRFR)
	- Delay (DELA)
		- Amount (AMOU)
		- Rate
		- Pingpong (PING)
			- Disabled (OFF)
			- Enabled (ON)
		- Type
			- Digital (DIGI)
			- Analog (ANA)
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
	- Reverb (REVE)
		- Amount (AMOU)
  			- Freeverb (FVRB)
          		- Mutable (MTBL)
		- Model (MODE)
		- Room Size (SIZE) (if Freeverb is Selected) or Time (if Mutable is Selected)
		- Damping (DAMP)
		- Width (WIDT) (if Freeverb is Selected) or Diffusion (DIFF) (if Mutable is Selected)
  		- HPF (if Mutable is selected)
		- Pan
		- Reverb Sidechain (SIDE)
			- Volume Ducking (VOLU)
	
	- Stutter (STUT)
		- Use Song Settings (SONG)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Stutter (STUT)
		- Use Song Settings (SONG)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Mod-FX (MODU)
		- Type
			- Disabled (OFF)
			- Flanger (FLAN)
			- Chorus (CHOR)
			- Phaser (PHAS)
			- Stereo Chorus (S.CHO)
			- Warble (WARB)
			- Grain (GRAI)
		- Rate
		- Depth (DEPT) (if Chorus, Phaser, Warble or Grain is selected)
			- Displayed as Grain Amount (Amnt) if Grain is selected
		- Feedback (FEED) (if Flanger, Phaser, Warble or Grain is selected)
			- Displayed as Grain Type (GRTY) if Grain is selected
		- Offset (OFFS) (if Chorus, Warble or Grain is selected)
			- Displayed as Grain Size (SIZE) if Grain is selected
	- Distortion (DIST)
		- Decimation (DECI)
		- Bitcrush (CRUS)
</details>
<details><summary>Sidechain (SIDE) </summary>

	- Volume Ducking (VOLU)
	- Sync
	NOTE: These options can change depending on how your default resolution is set

		- Off
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
	- Attack (ATTA)
	- Release (RELE)
	- Shape (TYPE)
</details>

</details>

<details>
<summary>MIDI Instrument Menu</summary>

The MIDI Instrument menu is accessible from MIDI clips by pressing on the `SELECT ENCODER`

The MIDI menu contains the following menu hierarchy:

<blockquote>
<details><summary>Device Definition (DEVI)</summary></details>

	This submenu will indicate whether the current MIDI Instrument in the Song is linked to a MIDI Device Definition file.

	- File Linked (LINK)
		- Unlinked
		- Linked
	- File Name of Linked File

<details><summary>PGM</summary></details>
<details><summary>Bank</summary></details>
<details><summary>Sub-Bank (SUB)</summary></details>
<details><summary>Arpeggiator (ARPE)</summary>

	- Enabled (ON)
	- Basic
		- Gate
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
		- Rate
	- Pattern
		- Octaves (OCTA)
		- Octave Mode (OMOD)
			- Up
			- Down
			- Up & Down (UPDN)
			- Alternate (ALT)
			- Random (RAND)
		- Chord Simulator (CSIM) (NOTE: Available only in Kit Rows)
		- Note Mode (NMOD)
			- Up
			- Down
			- Up & Down (UPDN)
			- Played Order (PLAY)
			- Random (RAND)
		- Step Repeat (REPE)
		- Rhythm (RHYT)
		- Sequence Length (LENG)
	- Randomizer
		- Lock Randomizer (LOCK)
		- Octave Spread (OCTA)
		- Gate Spread (GATE)
		- Velocity Spread (VELO)
		- Ratchet Amount (RATC)
		- Ratchet Probability (RPRO)
		- Chord Polyphony (CHRD) (NOTE: NOT available for Midi Kit Rows)
		- Chord Probability (CPRO) (NOTE: NOT available for Midi Kit Rows)
    	- Note Probability (NOTE)
    	- Swap Probability (SWAP)
    	- Bass Probability (BASS)
    	- Glide Probability (GLID)
	- MPE
		- Velocity (VELO)
			- Disabled (OFF)
			- Aftertouch
			- MPE Y (Y)
	- Include in Kit Arp (KARP) (NOTE: available only for Kit Rows)
</details>
<details><summary>Randomizer (RAND)</summary>

	- Velocity Spread (VELO)
	- Note Probability (NOTE)
</details>
<details><summary>Bend Range (BEND) </summary>

	- Normal (NORM)
	- Poly / Finger / MPE (MPE)
</details>
<details><summary>Poly Expression to Mono (POLY) </summary>

	- Aftertouch (AFTE)
		- Disabled (OFF)
		- Enabled (ON)
	- MPE
		- Disabled (OFF)
		- Enabled (ON)
</details>
<details><summary>Play Direction (DIRE) </summary>

	- Forward
	- Reversed
	- Ping-Pong
</details>

</details>

<details>
<summary>CV Instrument Menu</summary>

The CV Instrument menu is accessible from CV clips by pressing on the `SELECT ENCODER`

The CV menu contains the following menu hierarchy:

<blockquote>
<details><summary>Arpeggiator (ARPE)</summary>

	- Enabled (ON)
	- Basic
		- Gate
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
		- Rate
	- Pattern
		- Octaves (OCTA) (NOTE: NOT available for Gate Kit Rows)
		- Octave Mode (OMOD) (NOTE: NOT available for Gate Kit Rows)
			- Up
			- Down
			- Up & Down (UPDN)
			- Alternate (ALT)
			- Random (RAND)
		- Chord Simulator (CSIM) (NOTE: NOT available for Gate Kit Rows)
		- Note Mode (NMOD) (NOTE: NOT available for Gate Kit Rows)
			- Up
			- Down
			- Up & Down (UPDN)
			- Played Order (PLAY)
			- Random (RAND)
		- Step Repeat (REPE) (NOTE: NOT available for Gate Kit Rows)
		- Rhythm (RHYT)
		- Sequence Length (LENG)
	- Randomizer
		- Lock Randomizer (LOCK)
		- Octave Spread (OCTA) (NOTE: NOT available for Gate Kit Rows)
		- Gate Spread (GATE)
		- Velocity Spread (VELO) (NOTE: NOT available for Gate Kit Rows)
		- Ratchet Amount (RATC)
		- Ratchet Probability (RPRO)
		- Chord Polyphony (CHRD) (NOTE: NOT available for Gate Kit Rows)
		- Chord Probability (CPRO) (NOTE: NOT available for Gate Kit Rows)
		- Note Probability (NOTE)
    	- Swap Probability (SWAP) (NOTE: NOT available for Gate Kit Rows)
    	- Bass Probability (BASS) (NOTE: NOT available for Gate Kit Rows)
    	- Glide Probability (GLID) (NOTE: NOT available for Gate Kit Rows)
	- MPE (NOTE: NOT available for Gate Kit Rows)
		- Velocity (VELO)
			- Disabled (OFF)
			- Aftertouch
			- MPE Y (Y)
	- Include in Kit Arp (KARP) (NOTE: available only for Kit Rows)
</details>
<details><summary>Randomizer (RAND)</summary>

	- Velocity Spread (VELO)
	- Note Probability (NOTE)
</details>
<details><summary>Bend Range (BEND) </summary>

	- Normal (NORM)
	- Poly / Finger / MPE (MPE)
</details>
<details><summary>CV 2 Source (CV2) </summary>
	NOTE: This menu only becomes available if you set the CV / Gate channel to `1 AND 2 (BOTH)`

	- OFF
	- Y
	- Aftertouch
	- Velocity
</details>

<details><summary>Play Direction (DIRE) </summary>

	- Forward
	- Reversed
	- Ping-Pong
</details>

</details>

<details>
<summary>Instrument Clip Note Editor Menu</summary>

The Note Editor menu is accessible from Instrument Clips (Synth / Kit / MIDI / CV) by holding a `NOTE` pressing on the `SELECT ENCODER`.

This menu enables you to edit note specific parameters for a note in a selected step. If there are multiple notes in a step (e.g. it is a blurred note), it will adjust all notes in that step.

The Note Editor menu contains the following menu hierarchy:

<blockquote>
<details><summary>Note Velocity (VELO)</summary>
</details>
<details><summary>Note Probability (PROB)</summary>
</details>
<details><summary>Note Iterance (ITER)</summary>

	- 1 OF 2 (1OF2)
	- 2 OF 2 (2OF2)

	- 1 OF 3 (1OF3)
	- 2 OF 3 (2OF3)
	- 3 OF 3 (3OF3)

	- 1 OF 4 (1OF4)
	- 2 OF 4 (2OF4)
	- 3 OF 4 (3OF4)
	- 4 OF 4 (4OF4)

	- 1 OF 5 (1OF5)
	- 2 OF 5 (2OF5)
	- 3 OF 5 (3OF5)
	- 4 OF 5 (4OF5)
	- 5 OF 5 (5OF5)

	- 1 OF 6 (1OF6)
	- 2 OF 6 (2OF6)
	- 3 OF 6 (3OF6)
	- 4 OF 6 (4OF6)
	- 5 OF 6 (5OF6)
	- 6 OF 6 (6OF6)

	- 1 OF 7 (1OF7)
	- 2 OF 7 (2OF7)
	- 3 OF 7 (3OF7)
	- 4 OF 7 (4OF7)
	- 5 OF 7 (5OF7)
	- 6 OF 7 (6OF7)
	- 7 OF 7 (7OF7)

	- 1 OF 8 (1OF8)
	- 2 OF 8 (2OF8)
	- 3 OF 8 (3OF8)
	- 4 OF 8 (4OF8)
	- 5 OF 8 (5OF8)
	- 6 OF 8 (6OF8)
	- 7 OF 8 (7OF8)
	- 8 OF 8 (8OF8)

	- CUSTOM
		- Divisor (DIV)
		- Iteration 1 (IT 1)
		- Iteration 2 (IT 2)
		- Iteration 3 (IT 3)
		- Iteration 4 (IT 4)
		- Iteration 5 (IT 5)
		- Iteration 6 (IT 6)
		- Iteration 7 (IT 7)
		- Iteration 8 (IT 1)
</details>
<details><summary>Note Fill (FILL)</summary>

	- OFF
	- NOT FILL
	- FILL
</details>

</details>

<details>
<summary>Instrument Clip Note Row Editor Menu</summary>

The Note Row Editor menu is accessible from Instrument Clips (Synth / Kit / MIDI / CV) by holding an `AUDITION PAD` and pressing on the `SELECT ENCODER`.

This menu enables you to edit note row specific parameters for a selected note row. Adjusting a note row parameter will change that parameter for all notes in the note row.

The Note Row Editor menu contains the following menu hierarchy:

<blockquote>
<details><summary>Row Probability (PROB)</summary>
</details>
<details><summary>Row Iterance (ITER)</summary>

	- 1 OF 2 (1OF2)
	- 2 OF 2 (2OF2)

	- 1 OF 3 (1OF3)
	- 2 OF 3 (2OF3)
	- 3 OF 3 (3OF3)

	- 1 OF 4 (1OF4)
	- 2 OF 4 (2OF4)
	- 3 OF 4 (3OF4)
	- 4 OF 4 (4OF4)

	- 1 OF 5 (1OF5)
	- 2 OF 5 (2OF5)
	- 3 OF 5 (3OF5)
	- 4 OF 5 (4OF5)
	- 5 OF 5 (5OF5)

	- 1 OF 6 (1OF6)
	- 2 OF 6 (2OF6)
	- 3 OF 6 (3OF6)
	- 4 OF 6 (4OF6)
	- 5 OF 6 (5OF6)
	- 6 OF 6 (6OF6)

	- 1 OF 7 (1OF7)
	- 2 OF 7 (2OF7)
	- 3 OF 7 (3OF7)
	- 4 OF 7 (4OF7)
	- 5 OF 7 (5OF7)
	- 6 OF 7 (6OF7)
	- 7 OF 7 (7OF7)

	- 1 OF 8 (1OF8)
	- 2 OF 8 (2OF8)
	- 3 OF 8 (3OF8)
	- 4 OF 8 (4OF8)
	- 5 OF 8 (5OF8)
	- 6 OF 8 (6OF8)
	- 7 OF 8 (7OF8)
	- 8 OF 8 (8OF8)

	- CUSTOM
		- Divisor (DIV)
		- Iteration 1 (IT 1)
		- Iteration 2 (IT 2)
		- Iteration 3 (IT 3)
		- Iteration 4 (IT 4)
		- Iteration 5 (IT 5)
		- Iteration 6 (IT 6)
		- Iteration 7 (IT 7)
		- Iteration 8 (IT 1)
</details>
<details><summary>Row Fill (FILL)</summary>

	- OFF
	- NOT FILL
	- FILL
</details>
<details><summary>Play Direction (DIRE) </summary>

	- Forward
	- Reversed
	- Ping-Pong
</details>

</details>

<details>
<summary>Audio Clip Menu</summary>

The Audio Clip menu is accessible from Audio clips by pressing on the `SELECT ENCODER`

The Audio Clip menu contains the following menu hierarchy:

<blockquote>
<details><summary>Actions (ACT)</summary>
	- Set Clip Length to Sample Length (LEN)
</details>
<details><summary>Audio Source (AUDI)</summary>

	- Disabled (OFF)
	- Left Input (LEFT)
	- Left Input (Monitoring) (LEFT.)
	- Right Input (RIGH)
	- Right Input (Monitoring) (RIGH.)
	- Stereo Input (STER)
	- Stereo Input (Monitoring) (STER.)
	- Bal. Input (BALA)
	- Bal. Input (Monitoring) (BALA.)
	- Deluge Mix (Pre FX) (MIX)
	- Deluge Output (Post FX) (OUTP)
	- Specific Track (TRAK)
	- Specific Track (FX Processing) (TRAK.)
</details>
<details><summary>Specific Track (TRAK)</summary></details>
<details><summary>Master (MASTR)</summary>

	- Volume (VOLU)
	- Transpose (TRAN)
	- Pan
</details>
<details><summary>Compressor (COMP)</summary>

	- Threshold (THRE)
	- Ratio (RATI)
	- Attack (ATTA)
	- Release (RELE)
	- HPF
</details>
<details><summary>Filters (FLTR)</summary>

	- LPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Morph (MORP)
		- Mode (MODE)
			- 12DB Ladder (LA12)
			- 24DB Ladder (LA24)
			- Drive (DRIV)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
	- HPF
		- Frequency (FREQ)
		- Resonance (RESO)
		- Morph (MORP)
		- Mode (MODE)
			- SVF Bandpass (SV_B)
			- SVF Notch (SV_N)
			- HP Ladder (HP_L)
	- Filter Route (ROUT)
		- HPF2LPF (HPF2)
		- LPF2HPF (LPF2)
		- PARALLEL (PARA)
</details>
<details><summary>FX</summary>

	- EQ
		- Bass
		- Treble (TREB)
		- Bass Frequency (BAFR)
		- Treble Frequency (TRFR)
	- Delay (DELA)
		- Amount (AMOU)
		- Rate
		- Pingpong (PING)
			- Disabled (OFF)
			- Enabled (ON)
		- Type
			- Digital (DIGI)
			- Analog (ANA)
		- Sync
		NOTE: These options can change depending on how your default resolution is set

			- Off
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
	- Reverb (REVE)
		- Amount (AMOU)
  			- Freeverb (FVRB)
          		- Mutable (MTBL)
		- Model (MODE)
		- Room Size (SIZE) (if Freeverb is Selected) or Time (if Mutable is Selected)
		- Damping (DAMP)
		- Width (WIDT) (if Freeverb is Selected) or Diffusion (DIFF) (if Mutable is Selected)
  		- HPF (if Mutable is Selected)
		- Pan
		- HPF (if Mutable is selected)
		- LPF (if Mutable is selected)
		- Reverb Sidechain (SIDE)
			- Volume Ducking (VOLU)
	
	- Stutter (STUT)
		- Use Song Settings (SONG)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Stutter (STUT)
		- Use Song Settings (SONG)
		- Quantize (QTZ)
		- Reverse (REVE)
		- Ping-Pong (PING)

	- Mod-FX (MODU)
		- Type
			- Disabled (OFF)
			- Flanger (FLAN)
			- Chorus (CHOR)
			- Phaser (PHAS)
			- Stereo Chorus (S.CHO)
			- Warble (WARB)
			- Grain (GRAI)
		- Rate
		- Depth (DEPT) (if Chorus, Phaser, Warble or Grain is selected)
			- Displayed as Grain Amount (Amnt) if Grain is selected
		- Feedback (FEED) (if Flanger, Phaser, Warble or Grain is selected)
			- Displayed as Grain Type (GRTY) if Grain is selected
		- Offset (OFFS) (if Chorus, Warble or Grain is selected)
			- Displayed as Grain Size (SIZE) if Grain is selected
	- Distortion (DIST)
		- Saturation (SATU)
		- Decimation (DECI)
		- Bitcrush (CRUS)
</details>
<details><summary>Sidechain (SIDE) </summary>

	- Volume Ducking (VOLU)
	- Sync
	NOTE: These options can change depending on how your default resolution is set

		- Off
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
	- Attack (ATTA)
	- Release (RELE)
	- Shape (TYPE)
</details>
<details><summary>Sample (SAMP) </summary>

	- File Browser (FILE)
	- Reverse (REVE)
		- Disabled (OFF)
		- Enabled (ON)
	- Pitch/Speed (PISP)
		- Linked
		- Independent
	- Waveform (WAVE)
</details>
<details><summary>Attack (ATTA) </summary></details>
<details><summary>Priority (PRIO) </summary>

	- Low
	- Medium
	- High
</details>

</details>
