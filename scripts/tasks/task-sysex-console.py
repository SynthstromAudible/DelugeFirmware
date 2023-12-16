#! /usr/bin/env python3
import argparse
import subprocess
from datetime import datetime
import os
from pathlib import Path
import mido
from iterfzf import iterfzf

DEBUG_SYSEX_ID = 0x7D
DELUGE_PORT_DEFAULT_IN = "Deluge IN"
DELUGE_PORT_DEFAULT_OUT = "Deluge OUT"


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="sysex-console",
        description="Listens for deluge sysex and prints console messages"
    )
    parser.group = "Development"
    parser.add_argument(
        "-I", "--input", help="The device we are reading from", default=DELUGE_PORT_DEFAULT_IN
    )
    parser.add_argument(
        "-O", "--output", help="The device we notify we'll be reading from so they know to read to us", default=DELUGE_PORT_DEFAULT_OUT
    )

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

    input_ports=mido.get_input_names()
    print(f"Available input_ports: {input_ports}")
    input_device = args.input
    if input_device not in input_ports:
        def iter_input_ports():
            for port in input_ports:
                yield port
        input_device = iterfzf(iter_input_ports(), prompt = "'{device}' not found, please choose a port")

    output_ports=mido.get_output_names()
    print(f"Available output_ports: {output_ports}")
    output_device = args.output
    if output_device not in output_ports:
        def iter_output_ports():
            for port in output_ports:
                yield port
        output_device = iterfzf(iter_output_ports(), prompt = "'{device}' not found, please choose a port")

    now = datetime.now()  
    timestamp = now.isoformat()
    print(f"Using device: {input_device}/{output_device}")
    with mido.open_output(output_device) as output:
        output.send(mido.Message.from_bytes([
            0xf0,  # sysex message
            0x7d, # deluge (midi_engine.cpp midiSysexReceived)
            0x03,  # debug namespace
            0x00,   # sysex.cpp, sysexReceived
            0x01,   # sysex.cpp, sysexReceived 
            0xf7]))
    

    with mido.open_input(input_device) as port:
        for message in port:
            if message.type == 'sysex':
                if len(message.data) > 2 and message.data[1] == DEBUG_SYSEX_ID:
                    # Debug message 
                    print(bytes(message.data[5:-1]).decode('ascii'))
                    print(f'[{timestamp}] {bytes(message.data[5:-1]).decode("ascii")}')

    return 0

if __name__ == "__main__":
    main()
