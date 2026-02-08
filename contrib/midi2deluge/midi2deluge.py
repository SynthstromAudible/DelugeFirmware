#!/usr/bin/env python3

import mido
import os
import sys


def get_valid_screen_width(last_note_end):
    screen_width = 48
    while screen_width < last_note_end:
        screen_width *= 2
    return screen_width


def encode_note(position, length, velocity):
    return f"{position:08X}{length:08X}{velocity:02X}4014000000"


def convert_midi_to_xml(midi_file):
    mid = mido.MidiFile(midi_file)
    ppq_scale_factor = 96 / mid.ticks_per_beat
    midi_filename = os.path.splitext(os.path.basename(midi_file))[0]
    valid_tracks = []

    for i, track in enumerate(mid.tracks):
        notes = []
        current_time = 0
        track_name = None

        for msg in track:
            current_time += msg.time * ppq_scale_factor
            if msg.type == "track_name":
                track_name = msg.name
            if msg.type == "note_on" and msg.velocity > 0:
                notes.append(
                    {"midi": msg.note, "start": current_time, "velocity": msg.velocity}
                )
            if msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
                for note in notes[::-1]:
                    if "duration" not in note and note["midi"] == msg.note:
                        note["duration"] = current_time - note["start"]
                        break

        if notes:
            valid_tracks.append((track_name or f"Track{i + 1}", notes))

    if not valid_tracks:
        return  # Skip files with no valid tracks

    single_track = len(valid_tracks) == 1

    for track_index, (track_name, notes) in enumerate(valid_tracks):
        last_note_end = max(
            (n["start"] + n.get("duration", 0) for n in notes), default=0
        )
        lowest_pitch = min((n["midi"] for n in notes), default=60)
        screen_width = get_valid_screen_width(last_note_end)

        xml_output = '<?xml version="1.0" encoding="UTF-8"?>\n'
        xml_output += "<pattern>\n  <attributes\n"
        xml_output += f'    patternVersion="0.0.1"\n    screenWidth="{screen_width}"\n'
        xml_output += f'    scaleType="1"\n    yNoteOfBottomRow="{lowest_pitch}"/>\n  <noteRows>\n'

        note_map = {}
        for note in notes:
            if note["midi"] not in note_map:
                note_map[note["midi"]] = []
            note_map[note["midi"]].append(note)

        for pitch in sorted(note_map.keys()):
            pitch_notes = note_map[pitch]
            num_notes = len(pitch_notes)
            y_display = pitch - lowest_pitch
            xml_output += f'    <noteRow\n      numNotes="{num_notes}"\n      yNote="{pitch}"\n      yDisplay="{y_display}"\n'
            concatenated_notes = "".join(
                encode_note(int(n["start"]), int(n.get("duration", 1)), n["velocity"])
                for n in pitch_notes
            )
            xml_output += f'      noteDataWithSplitProb="0x{concatenated_notes}" />\n'

        xml_output += "  </noteRows>\n</pattern>"

        if single_track:
            output_file = os.path.join(
                os.path.dirname(midi_file), f"{midi_filename}.xml"
            )
        else:
            output_file = os.path.join(
                os.path.dirname(midi_file), f"{midi_filename}_{track_name}.xml"
            )

        with open(output_file, "w") as f:
            f.write(xml_output)
        print(f"Generated {output_file}")


def process_folder(folder_path, delete=False):
    for root, _, files in os.walk(folder_path):
        for file in files:
            if file.lower().endswith(".mid"):
                midi_path = os.path.join(root, file)
                convert_midi_to_xml(midi_path)
                if delete and "--delete" in sys.argv:
                    os.remove(midi_path)
                    print(f"Deleted {midi_path}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python midi2deluge.py <folder_path> [--delete]")
    else:
        delete_flag = "--delete" in sys.argv
        process_folder(sys.argv[1], delete=delete_flag)
