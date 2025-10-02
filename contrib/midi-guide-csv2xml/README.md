# Info

This python script allows you to generate Deluge MIDI device definitions from existing .csv files.

You can find a large database of .csv files in the following resources:
* https://midi.guide
* https://github.com/pencilresearch/midi 

# HowTo

Here's how to generate a Deluge MIDI devide definition from a CSV file:
* Download the desired CSV file from the resource above (https://midi.guide)
* Make sure Python is installed on your system
* Place the CSV file in the folder with this Python script
* Run `python3 midi-guide-csv2xml.py file.csv`
* A new Deluge XML file will be generated in the same folder with the same name, but with the .xml extension
* Copy this file to the MIDI_DEVICES/DEFINITION folder on your Deluge SD card
* Launch Deluge, select a MIDI track, press LOAD + UPPER GOLDEN KNOB to load the device definition file
* Now you can see the corresponding name for all available CC parameters
* Follow the official documentation to assign your favorite CC parameters to Golden knobs and save your personal presets.

# Sync
You can sync your copy of XML definitions with an existing GitHub repository of .csv files as follows:

```sh
git clone https://github.com/pencilresearch/midi
cd midi
find ./ -iname "*.csv" -exec ../midi-guide-csv2xml.py {} \;
rsync -rv --ignore-existing --include="*/" --include="*.xml" --exclude="*" ./ SD_CARD/MIDI_DEVICES/DEFINITION/
```
