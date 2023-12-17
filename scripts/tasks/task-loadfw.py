#! /usr/bin/env python3
from iterfzf import iterfzf
from pathlib import Path
from pathlib import Path
from tqdm import tqdm
import argparse
import datetime
import mido
import re
import os
import psutil
import shutil
import subprocess
import sys
import time

def platform_arch():
    # forgive me
    env_ssl_path = os.environ.get("SSL_CERT_FILE")
    match = re.search(r"/toolchain/([^/]+)/", env_ssl_path)
    if match:
        folder = match.group(1)
        return folder


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


def resolve_source_build(source_build):
    source_build_dir = Path(source_build).parent
    source_bins = [f for f in source_build_dir.glob('*.bin')]
    try:
        source_build_ts = os.path.getmtime(source_build)
        newer_bins = [f for f in source_bins if os.path.getmtime(f) > source_build_ts]
        if len(newer_bins) > 0:
            raise RuntimeError("Found newer bin files")
    except Exception as e:
        def iter_bins():
            bins = sorted(source_bins, key=lambda b: -os.path.getmtime(b)) 
            for bin in bins:
                print(bin)
                yield "\t".join([str(datetime.datetime.fromtimestamp(os.path.getmtime(bin))), str(bin)])
        chosen_build = iterfzf(iter_bins(), prompt=f"{e}, please confirm your build file.")
        source_build = chosen_build.split("\t")[1]
    return source_build

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

    source_build = resolve_source_build(args.build)
    target_bins = [f for f in Path(device_name).glob("*.bin")]
    target_build = 'deluge.bin'
    try:
        if len(target_bins) > 1:
            raise RuntimeError("Found multiple target overwrite candidates")
    except Exception as e:
        def iter_target_bins():
            for bin in target_bins:
                yield bin
        target_build = iterfzf(iter_target_bins(), prompt=f"{len(target_bins)} targets , please confirm your target build file.")


    # check if there is a newer ".bin" file in the same directory as args.build 
    # if so, confirm with the user if they are sure
    # if not, show them a list of bin files and their modified dates via iterfzf
    # set build to their choice

    try:
        dest = shutil.copy(source_build, os.path.join(device_name, target_build))
        source_ts = str(datetime.datetime.fromtimestamp(os.path.getmtime(source_build)))
        target_ts = str(datetime.datetime.fromtimestamp(os.path.getmtime(dest)))
            
        print(f"Copied {source_build} [{source_ts}] to {device_name} {dest} [{target_ts}]")
    except:
        print(f"Failed to copy {source_build} to {device_name}")
        raise

def load_sysex(args) -> None:
    loadfw_path = Path(f"./toolchain/{platform_arch()}/loadfw")
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
    source_build = resolve_source_build(args.build)
    # Invoke loadfw with the specified arguments
    loadfw_command = [str(loadfw_path), "-o", "output.syx", args.key, source_build]
    run_command(loadfw_command, args.verbose)

    # Use mido to send the syx file to the device
    if args.verbose:
        print(f"Sending syx file to {device}")

    MAX_KB_SEC = 300
    bytes_sent_sec = 0
    with mido.open_output(device) as output:
        for msg in tqdm(mido.read_syx_file("output.syx")):
            bytes_sent_sec +=len(msg.bin())
            if bytes_sent_sec > MAX_KB_SEC*1024:
                time.sleep(1)
                bytes_sent_sec = 0
            else:
                time.sleep(0.001)
            output.send(msg)
    print(f"Sent syx file: {args.build} to device {args.device}")

def main() -> int:
    args = argparser().parse_args()
    args.func(args)
    return 0

if __name__ == "__main__":
    main()
