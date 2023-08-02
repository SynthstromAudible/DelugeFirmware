# Deluge UX Principles
## What the Deluge Is

The Deluge is a standalone instrument that also works as a controller/sequencer. It has two core features which set it apart from the competition:

* Tracks can be smoothly changed from the internal synths to midi/cv and back again
* It features a linear arranger 

These features should be maintained and any new features must not impact them

## What the Deluge Isn't
The deluge is not a computer, and should not feel like you're operating one. Direct complexity and options should be minimized as much as possible, instead allowing the user to create complexity through combining existing features. An example of this is envelope shape - the envelope attack shape is fixed, but through modulating the attack rate with the envelope it can be smoothly morphed from logarithmic to exponential.

## Guidelines

* Favour explicitness 
    - Parameters describe their action on the waveform, not their result on the final sound
    - A user who understands synthesis should immediately be able to edit the deluge
* Favour simplicity
    - Menus should never feel mandatory
    - Make the shortcuts and gold knob system feel like a feature and not a hindrance 
    - The OLED screen is a bonus, not a requirement
* Favour performance
    - Entire parameter ranges should be useful and scaled for quick editing
    - Gold knob twists control frequently edited parameters
    - Gold knob presses switch modes, not scroll through parameter lists
    - Shortcuts control infrequently edited paramters
    - Menu-only items are reserved for rarely edited parameters
* Favour consistency
    - Actions which have similarity between views should have the same shortcut
      - i.e. `<>` + `back` clears all notes in a clip or all clips in the arranger
* Favour discoverability and intuitiveness
    - Leverage existing deluge UI paradigms
    - Integrate into existing UI structures
    - All parameters should have a menu entry


# Known UX issues

## Out of guidelines
This is a subset of issues where the current UX does not fully align with the principles above

* Horizontal scrolling is column by column in the arranger but screen by screen in the clip or song
* Clip nudging in the arranger is clip+ turn <>, note nudging in clip is note + press and turn <>

## Unclear

These are spots where there is ambiguity in the UI that could be improved

* Clips in song view are not clearly linked to tracks in arranger view. Perhaps the audition pads in arranger view could show the row colour (#163) from session view
* Linked/clone clips are not immediately identifiable in clip view - perhaps all linked clips should get the same row colour
* Sequencer parameters (probability, velocity, etc.) aren't accessible via the menu. Maybe holding a note and clicking select could open a sequencer menu?
