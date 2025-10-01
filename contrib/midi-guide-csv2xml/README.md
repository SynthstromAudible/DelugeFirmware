# Info

This python script allows you to generate Deluge MIDI device templates from existing .csv files.

You can find a large database of .csv files in the following resources:
* https://midi.guide
* https://github.com/pencilresearch/midi 

# HowTo

Here's how to generate a Deluge MIDI template from a CSV file:
* Download the desired CSV file from the resource above (https://midi.guide)
* Make sure Python is installed on your system
* Place the CSV file in the folder with this Python script
* Run `python3 midi-guide-csv2xml.py file.csv`
* A new Deluge XML file will be generated in the same folder with the same name, but with the .xml extension
* Copy this file to the MIDI folder on your Deluge SD card
* Launch Deluge, select a MIDI track, press UPPER GOLDEN KNOB + MIDI to load the preset, then select the new file.
