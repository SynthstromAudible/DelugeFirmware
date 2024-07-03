## **Feature: Automation View Note Velocity Editor**

New Note Velocity Editing View that has been added as part of the existing Automation View implementation so that you can edit the velocities and other parameters of notes in a single note row. 

<img width="605" alt="Screenshot 2024-06-05 at 11 34 47 AM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c0a1577c-5a10-4511-ab2e-1c40b3cb3d64">

### **Description:**

- imagine if the whole grid was just one note row, or one kit row
- each column of colours represents a note
- the vertical colours represents that notes velocity which you can edit by interacting with the grid (like you would with a parameter in automation view)

### **Quick Overview:**
- Enter the Velocity Editor View by:
   - While in the Automation Instrument Clip View, use Shift + Velocity or press Velocity pad on Automation Overview to enter the Velocity Note Editor
   OR
   - While in the Instrument Clip View, hold Audition pad + Press Velocity shortcut pad
- Select note row:
  - Press audition pad or Vertical scroll to select different note rows
  - The audition pad will blink to indicate the currently selected note row
  - The display will indicate the note code / drum name of the row you have selected
- Add Note / Adjust Velocity Using Grid:
  - Press grid to set/adjust velocities of existing notes and add note (if no note exists)
- Set a Velocity Ramp:
  - Press and hold two grid note pads to set a velocity ramp between the two notes. 
  - Note: for this to work, you must first press a pad that has a note in it and then press a second pad that has a note in it. if the second pad has no note already in it, it will set a tail instead for the first note pad pressed (or if it's a drum sound, like a kick, it will just add another note in the position of the second pad press).
- Remove Note:
  - Short press top velocity pad in a note column to remove note
- Select Note and Adjust Note Parameters:
  - Long press top velocity pad in a note column to see note's velocity and perform note related adjustment actions (e.g. adjust velocity, probability). Note press related actions include:
     - **Adjust Velocity:** Turn horizontal encoder <>
     - **Adjust Probability:** Turn Select Encoder
     - **Adjust Note Repeat:** Press and Turn Vertical Encoder
     - **Adjust Automation:** Turn gold knobs
- Adjust Note Row Transpose:
  - Press and turn vertical encoder
  - Note: does not work if you are holding a note (transpose gets replaced with editing note repeats)
- Adjust Note Row Probability:
  - Hold Audition Pad + Turn Select Encoder to change the selected Note Row's probability
     - Note: does not work if you are currently holding a note
- Add/Remove Notes with Euclidean:
   - Hold Audition Pad + Press and turn vertical encoder
   - Note: The Euclidean action is replaced by note repeat editing if you have selected a note (either by holding a note outside pad selection mode or if you are in pad selection mode)
- Change clip length:
   - Hold Shift + turn horizontal encoder <>
- Change selected note row length by:
   - Holding Audition pad + Turning horizontal encoder <>
   - Note: does not work if you have selected a note
- Clear notes in selected note row by: 
   - Holding horizontal encoder <> + pressing Back
   - Note: does not work if you have selected a note
- Rotate Note Row by:
   - Holding vertical encoder + turning horizontal encoder <>
   - Holding audition pad + pressing and turning horizontal encoder <>
   - Note: does not work if you have selected a note
- Quantize / Humanize notes by:
   - Enabling Quantize community feature
   - Hold audition pad + press and turn tempo encoder to set quantize / humanize %
- Cross Screen Editing:
    - Press cross screen while in the Velocity Note Editor to edit notes across multiple screens. 
    - Note: you can only toggle cross screen editing if the clip has multiple screens OR the current note row selected has multiple screens. 
