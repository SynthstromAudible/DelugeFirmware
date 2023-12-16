#! /usr/bin/env python3
import argparse
import subprocess
import os
from pathlib import Path
from iterfzf import iterfzf
import mido
import psutil
import shutil

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="loadfw",
        description="Loads the deluge firmware via SD or sysex (see docs/dev/getting_started.md)"
    )
    parser.group = "Development"
    parser.add_argument(
        "-B", "--build", help="The firmware image to be sent", default="build/Debug/deluge.bin"
    )
    parser.add_argument(
        "-V", "--verbose", help="Output additional information", default=is_verbose_mode()
    )
    subparsers = parser.add_subparsers()
    subparsers.required = True
    parser_sd = subparsers.add_parser('sd', help="Loads the deluge firmware via SD")
    parser_sd.add_argument(
        "-N", "--name", help="The volume name of the SD card"
    )
    parser_sd.set_defaults(func=load_sd)

    parser_sysex = subparsers.add_parser('sysex', help="Loads the deluge firmware via sysex")
    parser_sysex.add_argument(
        "-D", "--device", help="The device the firmware image is being sent to", default="Deluge OUT"
    )
    parser_sysex.add_argument(
        "-K", "--key", help="Your deluge insecure developer access key (See settings/community features/allow insecure)", required=True
    )
    parser_sysex.set_defaults(func=load_sysex)

    return parser

def is_verbose_mode() -> bool:
    return os.environ.get('LOADFW_VERBOSE', False) or os.environ.get('DBT_VERBOSE', False)

def run_command(command: list, verbose: bool) -> None:
    if verbose:
        print(f"Executing command: {' '.join(command)}")

    result = subprocess.run(command, capture_output=True, text=True)
    
    if verbose:
        print(f"Exit code: {result.returncode}")
        print(f"Output: {result.stdout}")
        if result.returncode != 0:
            print(f"Error: {result.stderr}")

    result.check_returncode()


def load_sd(args) -> None:
    # only list fstype=msdos
    device_name = args.name
    if device_name is None:
        disk_candidates = [part for part in psutil.disk_partitions() if part.fstype == 'msdos']
        if len(disk_candidates) == 0:
            raise RuntimeError("No deluge-compatible partitions found")
        print(f"Available disks: {disk_candidates}")
        def iter_disks():
            for disk in disk_candidates:
                yield disk.mountpoint

        device_name = iterfzf(iter_disks())
    try:
        dest = shutil.copy2(args.build, device_name)
    except:
        print(f"Failed to copy {args.build} to {device_name}")
        raise
    print(f"Copied {args.build} to {device_name} ({dest})")

def load_sysex(args) -> None:
    loadfw_path = Path("./toolchain/darwin-arm64/loadfw")
    # Check if loadfw exists, if not compile it
    if not loadfw_path.exists():
        compile_command = ["gcc", "./contrib/load/loadfw.c", "src/deluge/util/pack.c", "-Isrc/deluge", "-o", str(loadfw_path)]
        run_command(compile_command, args.verbose)

    ports = mido.get_output_names()
    def iter_ports():
        for port in ports:
            yield port
    device = args.device
    if device not in ports:
        print(f"'{device}' not found, please choose a port")
        device = iterfzf(iter_ports())

    # Invoke loadfw with the specified arguments
    loadfw_command = [str(loadfw_path), "-o", "output.syx", args.key, args.build]
    run_command(loadfw_command, args.verbose)

    # Use mido to send the syx file to the device
    if args.verbose:
        print(f"Sending syx file to {device}")

    with mido.open_output(device) as output:
        for msg in mido.read_syx_file("output.syx"):
            output.send(msg)
    print(f"Sent syx file: {args.build} to device {args.device}")

def main() -> int:
    args = argparser().parse_args()
    args.func(args)
    return 0

if __name__ == "__main__":
    main()
