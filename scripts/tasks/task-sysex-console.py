#! /usr/bin/env python3
from datetime import datetime
from iterfzf import iterfzf
from pathlib import Path
import argparse
import mido
import os
import subprocess
import sys

DEBUG_SYSEX_ID = 0x7D
DELUGE_PORT_DEFAULT_IN = "Deluge IN"
DELUGE_PORT_DEFAULT_OUT = "Deluge OUT"

# b.mtime
# gcc -shared -o scripts/util/pack.so -fPIC src/deluge/util/pack.c


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

def unpack_7bit_to_8bit(bytes):
  output = bytearray()

  for b in bytes:
    # Extract 7-bit value
    value = b & 0x7F
    
    # Extend to 8-bit
    if value & 0x40:
      value |= 0x80
    
    output.append(value)

    result = []
    for byte in bytes:
        result.append(byte & 0x7f)
        result.append(byte >> 7)
    return bytearray(result)

def unpack_7bit_to_8bit_rle(data):
  output = bytearray()

  i = 0
  while i < len(data):
    b = data[i]
    
    # If high bit set, next byte is repeat count
    if b & 0x80:  
      repeat = data[i+1] 
      i += 2
    else:
      repeat = 1
      i += 1
      
    # Extend 7-bit to 8-bit
    value = b & 0x7F
    if value & 0x40:
      value |= 0x80
    
    # Append repeated value 
    output.extend(bytes([value]) * repeat)

  return output

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
                if len(message.data) > 5 and message.bytes()[0:5] == [0xf0, 0x7d, 0x03, 0x40, 0x00]:
                  target_bytes = unpack_7bit_to_8bit(message.bin()[5:-1])
                  # Debug message 
                  decoded = target_bytes.decode('ascii')
                  sys.stdout.write(decoded.replace("\n", f"\n[{timestamp}] "))
    return 0

if __name__ == "__main__":
    main()
