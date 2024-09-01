# Looping in Grid View

## Description

There are two different looping techniques that can be used in the new Song Grid View:

1. Using green mode, create and immediately arm a clip within an existing track for recording
2. Select an existing clip and use the Global MIDI Commands: `LOOP` and `LAYERING LOOP `

The above techniques are described below

## Create and immediately arm a clip within an existing track for recording

This video demonstrates how to do it with an audio clip, using the microphone as the audio source, however it also works for arming non-audio clip's for recording.

https://github.com/user-attachments/assets/be1862c0-ef45-45fa-911b-8ec588246280

To use the technique shown in the video above, you must follow these steps:

- Enable Create + Record:
  - Enter `SETTINGS > DEFAULTS > UI > SONG > GRID > EMPTY PADS > CREATE + RECORD` and select `ENABLED`
  - Exit Settings menu to save settings

- The following steps enable you to quickly create and arm new clips for recording:
  - In Grid view, make sure you are in Green mode.
  - Press `PLAY` to start playback and press `RECORD` to enable recording.
  - Short-press any empty pad in an existing Instrument column to create and arm a new clip for recording
  - The new clip that was just created will be selected and start recording at the beginning of the next bar
  - You can press `RECORD` to stop recording or press the `NEW CLIP` to stop recording.
  - Repeat steps as required.

## Select an existing clip and use the Global MIDI Commands: LOOP and LAYERING LOOP

This technique requires the following:
- you have created a clip
- you have selected that clip by pressing and holding it from grid view or by entering it
- you are able to use the Global MIDI Commands: `LOOP` or `LAYERING LOOP` by either using the Grid Loop/Layering Loop pads or by sending the Loop/Layering Loop Global MIDI Commands using an external controller. 

To enable the Grid Loop/Layering Loop pads, enable `Grid View Loop Pads (LOOP)` in the `Community Features menu`.
 - When On, two pads (Red and Magenta) in the GRID VIEW sidebar will be illuminated and enable you to trigger the LOOP (Red) and LAYERING LOOP (Magenta) global MIDI commands to make it easier for you to loop in GRID VIEW without a MIDI controller.

After you have setup the above, you can now use the LOOP/LAYERING LOOP global MIDI commands in grid view.

**NOTE:** The mechanics of the looping workflow in Grid View using the LOOP/LAYERING LOOP global midi commands are still being worked on. Once the changes are finalized more information will be made available in this document.

