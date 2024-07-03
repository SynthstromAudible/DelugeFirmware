#! /usr/bin/env python3
import argparse
from pathlib import Path
from iterfzf import iterfzf
import datetime
import os

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="trace",
        description="Parse crash reader output into stack trace. e.g. ``",
    )
    parser.group = "Debugging"
    return parser


def parse_nmdump(file_path):
    with open(file_path, 'r') as file:
        nmdump_data = []
        for line in file:
            parts = line.split()
            if len(parts) >= 3:
                address = int(parts[0])  # Convert hex to int
                label = ' '.join(parts[2:])
                nmdump_data.append((address, label))
        nmdump_data.sort()  # Sort by address
    return nmdump_data

def find_closest_match(build_basename, nmdump_data, addresses):
    print("addr_hex\taddr_dec\taddr_src\tfile:line\tlabel")
    for addr in addresses:
        target = int(addr, 16)  # Convert hex to int
        closest_label = None
        for index, (nmdump_addr, label) in enumerate(nmdump_data):
            if nmdump_addr <= target:
                closest_label = f"{target}\t{nmdump_addr}\t{build_basename}:{index}\t{label}"
            else:
                break
        print(f"{addr}\t{closest_label}")

def input_addresses():
    print("Enter the addresses separated by newlines (press Enter twice to finish):")
    input_lines = []
    while True:
        line = input()
        if line == "":
            break
        input_lines.append(line)
    return input_lines


def main() -> int:
    source_build_dir = Path(f"{os.path.dirname(os.path.abspath(__file__))}/../../build/Debug")
    source_bins = [f for f in source_build_dir.glob('*.nmdump')]
    bins = sorted(source_bins, key=lambda b: -os.path.getmtime(b)) 

    def iter_bins():
        for bin in bins:
            yield "\t".join([str(datetime.datetime.fromtimestamp(os.path.getmtime(bin))), os.path.basename(bin)]) 

    chosen_display_option = iterfzf(iter_bins(), prompt=f"please confirm your target build file.")
    [tiem, build_basename] = chosen_display_option.split("\t")
    chosen_build = Path(source_build_dir, build_basename)
    data = parse_nmdump(chosen_build)
    addresses = input_addresses()
    find_closest_match(build_basename, data, addresses)
    return 0

if __name__ == "__main__":
    main()
