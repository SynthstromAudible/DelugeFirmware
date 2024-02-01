# Performance View

![20907a86d7c1dbc258b98cb31aced7b2fd7a8055_2_1332x1000](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/5b1f4def-9f74-4f76-85cd-24f645973a7c)

## Description:

Polyend Play inspired Performance View for the Deluge.

Each column represents a different "FX" and each pad/row in the column corresponds to a different FX value.

Specifications:
  - 16 FX columns
    - 1 Param assigned to each column
  - 8 FX values per column
    - Long press pads in a column to change value momentarily and reset it (to the value before the pad was pressed) upon pad release
    - Short press pads in a column to the change value until you press the pad again (resetting it to the value before the pad was pressed)
  - Editing mode to edit the FX values assigned to each pad and the parameter assigned to each FX column
  - Save defaults as xml file
    - Adjustable default Values assigned to each FX column via "Value" editing mode or xml
    - Adjustable default Param assigned to each FX column via "Param" editing mode or xml
    - Adjustable default "held pad" settings for each FX column via Performance View or xml (simply change a held pad in Performance View and save the layout to save the layout with the held pads).
  - Load defaults from xml file
  - Perform FX menu

## Usage:

#### 1) Enter performance view from song row view / arranger view by pressing the keyboard button / Enter performance view from song grid view by pressing the pink grid mode pad in the bottom right hand corner of the sidebar

#### 2) Short press a pad in the columns to hold that pad's value (parameter and held value is displayed on the screen)

- Note: If you save the layout while pad's are in a "held" state, the next time you re-load that layout it will re-load the held pad's and change the associated parameter's value based on the previous held state.

#### 3) Long press a pad in the columns to play the FX but not hold it (when you let go of the pad it will snap back to previous value). 

#### 4) You can edit the default values for each pad in the Performance View by entering a "Value Editing Mode." Here are the instructions for Value Editing mode:

> Enter using Shift + Keyboard button

or 

> Enter using the menu
> 
> - Enter/Exit Editing Mode by pressing down on the Select Encoder to open up the Perform FX Menu
> - In the Perform FX menu, select the "Editing Mode" sub menu and press down on Select Encoder to enter the menu
> - Change Editing Mode to "Value"

Value Editing Mode:
- While in the Value Editor, pressing any pad on the grid will open up the menu corresponding to the parameter for that pad. It will display the current value assigned to that pad when you pressed it.
- You can edit that pad's value by turning the select encoder while selecting a single pad (so it's highlighted white) or holding down on that pad. The updated value will be reflected on the display.
- After you have edited a value, the Save button will start flashing to indicate that you have "Unsaved" changes. Press and hold the save button + press the keyboard button to save your changes. Once saved, the Save button will stop blinking. To avoid being distracting, the save button will only blink when you are in editing mode. If you exit editing mode it will stop blinking and if you re-enter editing mode it will blink again to remind you that you have unsaved changes.
- You can re-load saved changes by pressing and holding the Load button + press the keyboard button. This will cause you to lose unsaved changes and the Save button will stop blinking.
- Defaults are saved in an XML file on your SD card - deleting the xml file will cause the PerformanceView to revert back to its regular default values for each pad.
- Once you are done with editing mode, repeat the steps above to set editing mode back to "Disabled" and exit out of the menu to use Performance View in its regular state

#### 5) You can edit the Parameter assigned to each FX column by entering a "Param Editing Mode." Here are the instructions for Param Editing mode:

> Enter using Shift + Keyboard button. You will need to cycle passed the Value Editing Mode to enter the Param Editing Mode.

or 

> Enter using the menu
> 
> - Enter/Exit Editing Mode by pressing down on the Select Encoder to open up the Perform FX Menu
> - In the Perform FX menu, select the "Editing Mode" sub menu and press down on Select Encoder to enter the menu
> - Change Editing Mode to "Param"

Param Editing Mode:
- Param Editing will show you an overview of the parameters that are assignable to the FX columns in Performance View by lighting dimly the shortcut pads for these parameters.
- Parameters that have been assigned to one or more columns will have their shortcut pads lit up bright white.
- Press on a shortcut pad to illuminate the FX columns that that parameter has been assigned to.
- While holding a shortcut pad, press on the FX columns to assign or unassign a parameter to/from a column.
- Press <> + back to clear all existing Parameter assignments.
- When a Parameter has not been assigned to a column, that column will be lit grey and be unusable in Performance View until you assign a Parameter. This applies to editing the values for that FX column as well (assign a Parameter first, then you can edit the values).
- Parameters are saved to xml. You can manually edit the Parameters in the xml as well, but you must use the exact Parameter names. It is recommended to save a fresh xml and back it up so you have a record of the Parameter Names.

#### 6) You can Undo/Redo your changes in Performance View

## Default FX and Colour Assignments
* The default Param and Colour Assignments for each FX column in Performance View are as follows
* Note: if you re-arrange the parameters to different FX columns, they will bring the colours noted below with them.

Columns:

Red:
1 = LPF Cutoff
2 = LPF Resonance

Orange:
3 = HPF Cutoff
4 = HPF Resonance

Yellow:
5 = Bass EQ
6 = Treble EQ

Light Green:
7 = Reverb

Light Blue:
8 = Delay Amount
9 = Delay Rate

Light Pink:
10 = MOD-FX Rate
11 = MOD-FX Depth
12 = MOD-FX Feedback
13 = MOD-FX Offset

Dark Pink:
14 = Decimation
15 = Bitcrush

Dark Blue:
16 = Stutter Rate + Stutter Trigger

# Morph Mode and Alternate Layouts for Performance View

<img width="349" alt="Screenshot 2024-02-06 at 10 10 37 PM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/09c36028-9679-4184-885c-9226e9654337">

# Description and Instructions

## Alternate Layouts

- There are 9 total performance view layout variants:
  - Default, 1, 2, 3, 4, 5, 6, 7, 8
- Layout can be loaded from / saved to your SD card. They are stored in a folder titled PERFORMANCE_VIEW.
<img width="254" alt="Screenshot 2024-02-06 at 9 30 38 PM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/1c2fde68-f9c0-4f36-be2d-61c027ada132">

### Loading the Default Layout
  - Hold Load + Press Keyboard to load the Default Layout
  - The layout from Default.XML will be loaded

### Saving the Default Layout
  - Hold Save + Press Keyboard to save the Default Layout
  - The layout will be saved to Default.XML
  
### Loading the Alternate Layouts  
 - To load the 8 alternate layouts, you need to first select a Layout Bank.
    - There are two banks of 4 layouts.
    - To select Bank 1, press the Scale button, it will light up.
    - To select Bank 2, press the Cross-Screen button, it will light up.
 - After selecting a bank:
    - Hold Load + Press Synth, Kit, Midi, or CV to load the layout corresponding to those 4 buttons and the bank selected.
 - Bank 1:
   - Synth = 1.XML
   - Kit = 2.XML
   - Midi = 3.XML
   - CV = 4.XML
 - Bank 2:
   -  Synth = 5.XML
   - Kit = 6.XML
   - Midi = 7.XML
   - CV = 8.XML
- When you load a layout, a pop up is displayed to tell you the layout you just loaded (e.g. Default, 1, 2, 3, 4, 5, 6, 7, or 8)

### Saving the Alternate Layouts  
- Use the instructions for Loading a layout above, except instead of holding the Load button, you hold the Save button.
  - Hold Save + Press Keyboard to save layout changes to Default.XML
  - Selected Bank 1 or 2 using Scale or Cross-screen:
    - While in Bank 1, Hold Save + Press Synth/Kit/Midi/CV to save to 1.XML, 2.XML, 3.XML, or 4.XML
    - While in Bank 2, Hold Save + Press Synth/Kit/Midi/CV to save to 5.XML, 6.XML, 7.XML, or 8.XML
    
### Check what layout is currently loaded
- At any time you can check what layout is currently loaded by pressing on the Bank 1 or Bank 2 buttons (Scale or Cross-screen).
  - A pop-up will be shown not he display that tells you the name of the layout that is currently loaded (Default, 1, 2, 3, 4, 5, 6, 7, or 8)
  - If you hold the bank button, the layout currently loaded for the bank selected (if there is one) will also flash to indicate that that layout is loaded (e.g. if you are holding the Bank 1 button (Scale) and Layout 1 is loaded, the Synth button will flash)

## Morph Mode

Morph Mode is a new sub-mode in Performance View that lets you load two layouts into two layout banks for morphing between the two layouts. I will refer to these banks as Bank A and Bank B
  - Bank A and Bank B are accessed in Morph Mode with the Synth and CV buttons (Synth = Bank A, CV = Bank B)
  
### Entering Morph Mode  
- To enter Morph Mode, press both the Scale button and Cross-screen buttons together
  - To signal that you are in Morph Mode, the Scale button and Cross-screen buttons will be lit up
  - The gold encoder led indicators will also be reset and the mod buttons between the gold encoders will turn off
    - While in morph mode, you cannot control / change parameters using the gold encoders
    
### Assign a layout to Bank A and Bank B    
- After you have entered Morph Mode, you will need to assign a layout to Bank A and Bank B
  - Note: the layouts you assign to Bank A and Bank B must be compatible for Morphing. This means:
    - They must have the same FX to column layout structure. E.g. If LPF frequency is in Column 1 in the layout added to Bank A, it must also be in Column 1 in the layout added to Bank B. If you attempt to Morph with two incompatible layouts, you will receive a pop-up saying "Can't Morph"
    - If both layouts are compatible, either the Bank A button (Synth) or Bank B button (CV) will light up (depending on which bank you assigned a layout to last).
    - Note: another reason for a "Can't Morph" message is that the layouts assigned to Bank A and Bank B (both) do not have any held values. As soon as you assign a layout to Bank A or B with a held value, they become compatible for morphing.
  - To assign a layout to Bank A, hold Synth + turn the Select encoder to selected between the 9 layouts available (Default, 1, 2, 3, 4, 5, 6, 7, 8)
  - To assign a layout to Bank B, hold CV + turn the Select encoder to selected between the 9 layouts available (Default, 1, 2, 3, 4, 5, 6, 7, 8)
- After you have assigned two compatible layouts to Bank A and Bank B, you are ready to Morph.

### Morphing between Layouts
- To Morph between the layouts stored in Banks A and B, use either Gold (Mod) encoder.

### Indicator's of your morph position between the layout in Bank A and Bank B
- The display will show a value range of -50 to +50 as you turn the Gold encoder.
- The Gold encoder LED indicators will also light up to indicate your position
- The Synth/Kit/Midi/CV buttons will also light up to show your position.
  - Synth button lit up = 100% Layout A
  - Synth and Kit buttons lit up = between 100% and 75% Layout A
  - Kit button lit up = 75% Layout A, 25% Layout B
  - Kit and Midi buttons lit up = 50% Layout A, 50% Layout B
  - Midi button lit up = 25% Layout A, 75% Layout B
  - Midi and CV buttons lit up = between 100% and 75% Layout B
  - CV button lit up = 100% Layout B
  
### How does Morphing Work?
- Morphing works by morphing between the Held values for the same parameters in the two layouts assigned to Bank A and Bank B
- If only one layout has Held values, it means that it will Morph between the held value and the release value saved in that one layout (the other Layout in the other Bank will have no effect on the morphing).
  - The release value is the value that the parameter would be set back to if you removed the held pad.
  - Whenever you add a held pad, a snapshot of the current value is stored. It is this snapshot that is used for morphing in this scenario.
- When you have reached either end of the Morph spectrum (e.g. 100% Bank A or 100% Bank B), the layout loaded on the grid is refreshed. For example, if you started with Bank A, you will see Bank A loaded on the grid with any held pads. Once you have morphed over to Bank B, the grid will be refreshed and show the held pads from Bank B.
  
### Changing layout Selection while in Morph Mode
- While in Morph mode, if you press on the Bank A button or Bank B button, it will load the layout saved in that bank.
  
### Non-Destructive / Temporary Morph Layout Changes
- You can make quick changes to the layouts assigned to Morph A and B without needing to save those layouts
- For example, you could load the Default layout into Bank A and Bank B
- To then make changes to the Default layout stored in Bank A and Bank B, do the following:
  - Hold the Bank A button (Synth) and change any of the held pads on the Performance View grid
  - Let go of the Bank A button. The changes you made are now temporarily saved in Bank A
  - You can do the same for Bank B
  - You now have two temporary variants of the Default layout
- You could use this technique to test out changes to your morph layouts, and then when you like what you've found, you save those temporary layouts to one of the 9 layout files by repeating the Bank 1/Bank 2 save steps above.

### The OLED display
- Just a quick overview of the OLED display for Morph Mode
- While in Morph Mode, the OLED display show in the bottom middle a cross-fader to show the morph position between Bank A and Bank B
- To the left and right side of the cross fader, if a layout has been assigned to Bank A and Bank B, it will display the name of the layout assigned to Bank A and Bank B (e.g. D for default, 1, 2, 3, 4, 5, 6, 7, 8)
- Right above the cross-fader, you will see "Can't Morph" if the layouts assigned to Bank A and Bank B are not compatible.

## Midi-Learning Morph Mode

The control for morphing between Bank A and Bank B in Morph Mode has been made MIDI Learnable so that you can Morph between Bank A and Bank B from anywhere in the Deluge (e.g. whether you're in a Song View or Clip View).

You can MIDI control the Morph Mode cross-fader by learning your MIDI controller to the Deluge using one two methods:

1. Global MIDI Commands
2. MIDI Follow Mode

### Global MIDI Command :: MORPH

- To learn an external controller to the Morph Mode control for use with the Deluge's Global MIDI Command's, you need to access the following menu:
  - SETTINGS -> MIDI -> COMMANDS -> MORPH
- While in the MORPH menu, Hold Learn + Send a Note/CC to learn the device.
- At any time you can unlearn the Morph control by pressing Shift + Learn while in the MORPH menu
- Exit the menu to save your settings

## Saving Layouts with the Song

- The layout you currently have loaded and the layouts assigned to Bank A and Bank B in Morph Mode get saved with the song.
- When you load your song again, it will load the layout used in that song from the SD card folder PERFORMANCE_VIEW.
