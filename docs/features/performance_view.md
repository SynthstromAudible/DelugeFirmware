# Performance View

![20907a86d7c1dbc258b98cb31aced7b2fd7a8055_2_1332x1000](https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/5b1f4def-9f74-4f76-85cd-24f645973a7c)

## Description:

Polyend Play inspired Performance View for the Deluge.

Each column represents a different "FX" and each pad/row in the column corresponds to a different FX value.

Specifications:
  - Perform FX menu to edit Song level parameters and enable/disable editing mode.
  - 16 FX columns
    - 1 Param assigned to each column
  - 8 FX values per column
    - Long press pads in a column to change value momentarily and reset it (to the value before the pad was pressed) upon pad release
    - Short press pads in a column to the change value until you press the pad again (resetting it to the value before the pad was pressed)
    - Quickly clear all held values using `HORIZONTAL ENCODER ◀︎▶︎` + `BACK` (resetting FX values back to their previous state)
  - Editing mode to edit the FX values assigned to each pad and the parameter assigned to each FX column
  - Save defaults as PerformanceView.xml file
    - Adjustable default Values assigned to each FX column via `VALUE` editing mode or PerformanceView.xml
    - Adjustable default Param assigned to each FX column via `PARAM` editing mode or PerformanceView.xml
    - Adjustable default "held pad" settings for each FX column via Performance View or PerformanceView.xml (simply change a held pad in Performance View and save the layout to save the layout with the held pads).
  - Load defaults from PerformanceView.xml file

## Usage:

#### 1) Enter performance view from song row view / arranger view by pressing the keyboard button / Enter performance view from song grid view by pressing the pink grid mode pad in the bottom right hand corner of the sidebar

#### 2) Short press a pad in the columns to hold that pad's value (parameter and held value is displayed on the screen)

- Note: If you save the layout while pad's are in a "held" state, the next time you re-load that layout it will re-load the held pad's and change the associated parameter's value based on the previous held state.

#### 3) Long press a pad in the columns to play the FX but not hold it (when you let go of the pad it will snap back to previous value). 

#### 4) Press `HORIZONTAL ENCODER ◀︎▶︎` + `BACK` to clear all "held" FX values.

#### 5) You can edit the default values for each pad in the Performance View by entering a `VALUE EDITING MODE`. Here are the instructions for Value Editing mode:

> Enter using `SHIFT` + `KEYBOARD` button

or 

> Enter using the menu
> 
> - Enter/Exit Editing Mode by pressing down on the `SELECT` Encoder to open up the Perform FX Menu
> - In the Perform FX menu, select the `EDITING MODE` sub menu and press down on Select Encoder to enter the menu
> - Change Editing Mode to `VALUE`

Value Editing Mode:
- While in the Value Editor, pressing any pad on the grid will open up the menu corresponding to the parameter for that pad. It will display the current value assigned to that pad when you pressed it.
- You can edit that pad's value by turning the select encoder while selecting a single pad (so it's highlighted white) or holding down on that pad. The updated value will be reflected on the display.
- After you have edited a value, the Save button will start flashing to indicate that you have "Unsaved" changes. Press and hold the save button + press the keyboard button to save your changes. Once saved, the Save button will stop blinking. To avoid being distracting, the save button will only blink when you are in editing mode. If you exit editing mode it will stop blinking and if you re-enter editing mode it will blink again to remind you that you have unsaved changes.
- You can re-load saved changes by pressing and holding the Load button + press the keyboard button. This will cause you to lose unsaved changes and the Save button will stop blinking.
- Defaults are saved in an XML file on your SD card called "PerformanceView.XML" - deleting this file will cause the PerformanceView to revert back to its regular default values for each pad.
- Once you are done with editing mode, repeat the steps above to set editing mode back to "Disabled" and exit out of the menu to use Performance View in its regular state

#### 6) You can edit the Parameter assigned to each FX column by entering a `PARAM` Editing Mode. Here are the instructions for Param Editing mode:

> Enter using `SHIFT` + `KEYBOARD` button. You will need to cycle passed the Value Editing Mode to enter the Param Editing Mode.

or 

> Enter using the menu
> 
> - Enter/Exit Editing Mode by pressing down on the `SELECT` Encoder to open up the Perform FX Menu
> - In the Perform FX menu, select the `EDITING MODE` sub menu and press down on Select Encoder to enter the menu
> - Change Editing Mode to `PARAM`

Param Editing Mode:
- Param Editing will show you an overview of the parameters that are assignable to the FX columns in Performance View by lighting dimly the shortcut pads for these parameters.
- Parameters that have been assigned to one or more columns will have their shortcut pads lit up bright white.
- Press on a shortcut pad to illuminate the FX columns that that parameter has been assigned to.
- While holding a shortcut pad, press on the FX columns to assign or unassign a parameter to/from a column.
- Press `HORIZONTAL ENCODER ◀︎▶︎` + `BACK` to clear all existing Parameter assignments.
- When a Parameter has not been assigned to a column, that column will not be lit and will be unusable in Performance View until you assign a Parameter. This applies to editing the values for that FX column as well (assign a Parameter first, then you can edit the values).
- Parameters are saved to PerformanceView.xml. You can manually edit the Parameters in the xml as well, but you must use the exact Parameter names. It is recommended to save a fresh PerformanceView.xml and back it up so you have a record of the Parameter Names.

#### 7) You can Undo/Redo your changes in Performance View

## Default FX and Colour Assignments
* The default Param and Colour Assignments for each FX column in Performance View are as follows
* Note: if you re-arrange the parameters to different FX columns (e.g. using Param Editing Mode), they will bring the colours noted below with them.
* Params marked "Unassigned" below are available to be assigned to a Performance View column with Param Editing Mode.

Columns:

Red:
1 = LPF Cutoff
2 = LPF Resonance
Unassigned = LPF Morph

Orange:
3 = HPF Cutoff
4 = HPF Resonance
Unassigned = HPF Morph

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
