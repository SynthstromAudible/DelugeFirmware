#!/usr/bin/python3
# This script converts a CSV file from https://midi.guide with MIDI device definitions
# into Deluge's XML file for the sd_card/MIDI_DEVICES/DEFINITION directory.
import sys
import csv

# .csv column names. You can redefine them if needed (see your input .csv file headers)
csv_col_midiName = "parameter_name"
csv_col_midiCC = "cc_msb"

# check args
try:
    file_name_in = sys.argv[1]
except IndexError:
    print(f"USE: {sys.argv[0]} <file.csv>")
    sys.exit()
file_name_out = file_name_in.replace(".csv", ".xml")

# read csv as a dict
try:
    csv_data = csv.DictReader(open(file_name_in))
except OSError:
    print(f"ERROR: Could not read {file_name_in} file")
    sys.exit()

# generage xml
with open(file_name_out, "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<midiDevice>\n\t<ccLabels')
    # check all csv data and select only needed
    for row in csv_data:
        midiName = ""
        midiCC = ""
        for k, v in row.items():
            if k == csv_col_midiName:
                midiName = v
            if k == csv_col_midiCC:
                midiCC = v
        if len(midiName) > 0 and len(midiCC) > 0:
            f.write(f'\n\t\t{midiCC}="{midiName}"')
    f.write("/>\n</midiDevice>")
