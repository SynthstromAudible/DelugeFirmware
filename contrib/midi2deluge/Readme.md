Midi2Deluge Batch Converter

## Overview
This script recursively searches through a specified folder for .mid files, converts them to Deluge Pattern format, and optionally deletes the original .mid files.

## Features
- Recursively processes all .mid files in a given folder.
- Converts each MIDI file into Deluge Pattern Format.
- If a MIDI file has only one non-empty track, the output file name matches the input.
- If a MIDI file has multiple tracks, each track is saved separately with the format <filename>_trackname.xml.
- Supports an optional --delete flag to remove the original .mid files after conversion.

## Installation
Ensure you have Python installed along with the mido library:

sh
pip install mido


## Usage
Run the script with the path to the folder containing MIDI files:

sh
python midi2deluge.py <folder_path>


To delete the original .mid files after conversion, add the --delete flag:

sh
python midi2deluge.py <folder_path> --delete


## Example
sh
python midi2deluge.py ./midis --delete

This will process all .mid files inside the midis folder, convert them to XML, and delete the original .mid files.

## Notes
- If no valid tracks are found in a MIDI file, it will be skipped.
- The script ensures the generated XML files maintain proper formatting.

## License
This script is released under the MIT License.
