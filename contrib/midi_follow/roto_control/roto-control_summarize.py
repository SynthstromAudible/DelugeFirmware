import json
import argparse
import os
from collections import Counter

DESCRIPTION = """
Summarize Melbourne Instruments Roto-Control configuration JSON files into a markdown table.
Currently, only knobs are considered, ignoring buttons.
"""

def parse_json_files(files, verbose):
    rows = []
    midi_channels = []  # Store only valid MIDI channels

    for file in files:
        if verbose:
            print(f"Processing file: {file}")
        with open(file, 'r', encoding='utf-8') as f:
            data = json.load(f)
            setup_name = data.get("name", "Unknown Setup")
            seen_positions = set()  # Reset per file

            for knob in data.get("knobs", []):
                position = knob["controlIndex"] + 1
                page = ((position - 1) // 8) + 1  # Calculate page (1-4)
                midi_channel = knob.get("controlChannel")

                if midi_channel not in ["NA", None, ""]:
                    midi_channels.append(midi_channel)
                    if verbose:
                        print(f"Found MIDI Channel: {midi_channel} for Position {position}")

                seen_positions.add(position)
                row = {
                    "Parameter": f"**{knob['controlName']}**" if knob.get('controlName') else "",
                    "Position": position,
                    "Page": page,
                    "Setup": setup_name,
                    "CC#": str(knob.get("controlParam", "")) if knob.get("controlParam", 0) != 0 else "",
                    "MIDI Channel": midi_channel if midi_channel not in ["NA", None, ""] else None
                }
                rows.append(row)

            # Ensure all 32 positions exist for each setup
            for pos in range(1, 33):
                if pos not in seen_positions:
                    if verbose:
                        print(f"Empty knob at position {pos} for {setup_name}, ensuring no MIDI channel assigned.")
                    page = ((pos - 1) // 8) + 1
                    row = {
                        "Parameter": "",
                        "Position": pos,
                        "Page": page,
                        "Setup": setup_name,
                        "CC#": "",
                        "MIDI Channel": None
                    }
                    rows.append(row)

    if verbose:
        print(f"Final MIDI channels found: {midi_channels}")

    # Sort the rows by Setup name and Position
    rows.sort(key=lambda x: (x['Setup'], x['Position']))

    return rows, midi_channels

def generate_markdown_table(rows, midi_channels, single_setup=False):
    defined_midi_channels = [ch for ch in midi_channels if ch is not None]
    midi_channel_counts = Counter(defined_midi_channels)
    most_common_channel, _ = midi_channel_counts.most_common(1)[0] if defined_midi_channels else (None, 0)
    unique_channels = set(defined_midi_channels)

    include_midi_column = len(unique_channels) > 1

    # Build header
    markdown = "| Parameter | Position | Page "
    if not single_setup:
        markdown += "| Setup "
    markdown += "| CC# "
    if include_midi_column:
        markdown += "| MIDI Channel "
    markdown += "|\n"

    # Build header divider
    markdown += "|-----------|----------|------|"
    if not single_setup:
        markdown += "-------|"
    markdown += "----|"
    if include_midi_column:
        markdown += "--------------|"
    markdown += "\n"

    # Build table rows
    for row in rows:
        markdown += f"| {row['Parameter']} | {row['Position']} | {row['Page']} |"
        if not single_setup:
            markdown += f" {row['Setup']} |"
        markdown += f" {row['CC#']} |"
        if include_midi_column:
            midi_channel = row["MIDI Channel"]
            if midi_channel is not None:
                midi_display = str(midi_channel) if midi_channel == most_common_channel else f"**{midi_channel}**"
            else:
                midi_display = ""
            markdown += f" {midi_display} |"
        markdown += "\n"

    # Append MIDI channel summary if needed
    if len(unique_channels) == 1 and most_common_channel is not None:
        markdown += f"\nMIDI channel: {most_common_channel}\n"
    elif len(unique_channels) > 1:
        midi_channels_list = ", ".join(map(str, unique_channels))
        markdown += f"\nMIDI channels: {midi_channels_list}\nWARNING: Multiple MIDI channels detected. Is this intentional?\n"

    # If single file, add setup info below table
    if single_setup and rows:
        setup_name = rows[0]['Setup']
        markdown += f"\nSetup: {setup_name}\n"

    return markdown

def generate_output_filename(files):
    base_name = "_".join([os.path.splitext(os.path.basename(f))[0] for f in files])
    return f"roto_{base_name}.md"

def main():
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument("files", nargs='+', help="One or more JSON files to parse.")
    parser.add_argument("--output", "-o", help="Optional output file to save markdown table.")
    parser.add_argument("--verbose", "-v", action="store_true", help="Enable verbose debugging output.")

    args = parser.parse_args()
    rows, midi_channels = parse_json_files(args.files, args.verbose)

    single_setup = len(args.files) == 1
    markdown_table = generate_markdown_table(rows, midi_channels, single_setup)

    output_file = args.output if args.output else generate_output_filename(args.files)

    if args.verbose:
        print("\nGenerated Markdown Table:\n")
        print(markdown_table)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(markdown_table)
    print(f"Markdown table saved to {output_file}")

if __name__ == "__main__":
    main()
