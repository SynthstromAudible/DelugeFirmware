#! /usr/bin/env python3
import argparse
import subprocess
from datetime import datetime
import os
from pathlib import Path
import mido
from iterfzf import iterfzf

DEBUG_SYSEX_ID = 0x7D
DELUGE_PORT_DEFAULT = "Deluge IN"


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="sysex-console",
        description="Listens for deluge sysex and prints console messages"
    )
    parser.group = "Development"
    parser.add_argument(
        "-D", "--device", help="The device the firmware image is being sent to", default=DELUGE_PORT_DEFAULT)

    return parser

def is_verbose_mode() -> bool:
    return os.environ.get('SYSEX_CONSOLE_VERBOSE', False) or os.environ.get('DBT_VERBOSE', False)

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

def main() -> int:
    args = argparser().parse_args()
    verbose = is_verbose_mode()

    ports=mido.get_input_names()
    print(f"Available ports: {ports}")
    def iter_ports():
        for port in ports:
            yield port
    device = args.device
    if device not in ports:
        print(f"'{device}' not found, please choose a port")
        device = iterfzf(iter_ports())

    now = datetime.now()  
    timestamp = now.isoformat()
    print(f"Using device: {device}")

    with mido.open_input(device) as port:
        for message in port:
            if message.type == 'sysex':
                if message.data[1] == DEBUG_SYSEX_ID:
                    # Debug message 
                    print(bytes(message.data[5:-1]).decode('ascii'))
                    print(f'[{timestamp}] {bytes(message.data[5:-1]).decode("ascii")}')

    return 0

if __name__ == "__main__":
    main()
