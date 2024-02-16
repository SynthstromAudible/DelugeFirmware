#! /usr/bin/env python3
import sys
import time
import argparse
import rtmidi
import sys


def argparser():
    midiout = rtmidi.MidiOut()
    available_ports = midiout.get_ports()
    s = "\nusage example: dbt sysex-logging 123"
    s += "\n\nstart listening to console logs on MIDI port # 123"
    s += "\n\nAvailable MIDI ports:\n\n"
    for i, p in enumerate(available_ports):
        s += "Port # " + str(i) + " : " + str(p) + "\n"

    parser = argparse.ArgumentParser(
        prog="sysex-logging",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Listen to console logs via MIDI",
        epilog=s,
        exit_on_error=True,
    )
    parser.group = "Development"
    parser.add_argument(
        "port_number",
        help="MIDI port number (example: 123) use 'dbt sysex-logging -h' to list available ports",
        type=int,
    )
    return parser


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
            result.append(byte & 0x7F)
            result.append(byte >> 7)
        return bytearray(result)


def sysex_console(port):
    midiout = rtmidi.MidiOut()
    midiout.open_port(port)

    with midiout:
        data = bytearray(9)
        # main Deluge header
        data[0] = 0xF0  # sysex message
        data[1] = 0x00
        data[2] = 0x21
        data[3] = 0x7B
        data[4] = 0x01

        data[5] = 0x03  # debug namespace
        data[6] = 0x00  # 0x00 is the command for sysex logging configuration
        data[7] = 0x01  # 0x01 = enable, 0x00 = disable
        data[8] = 0xF7
        midiout.send_message(data)
    del midiout

    midiin = rtmidi.MidiIn()
    midiin.open_port(port)
    midiin.ignore_types(False, True, True)

    while True:
        msg_and_dt = midiin.get_message()
        if msg_and_dt:
            # unpack the msg and time tuple
            (msg, _) = msg_and_dt
            if (
                msg[0] == 0xF0
                and len(msg) > 5
                and msg[0:8]
                == [
                    # main Deluge sysex header
                    0xF0,
                    0x00,
                    0x21,
                    0x7B,
                    0x01,
                    # debug namespace
                    0x03,
                    # debug log message command
                    0x40,
                    0x00,
                ]
            ):
                target_bytes = unpack_7bit_to_8bit(msg[5:-1])
                decoded = target_bytes.decode("ascii").replace("\n", "")
                print(decoded)
        else:
            # add a short sleep so the while loop doesn't hammer your cpu
            time.sleep(0.01)


def main():
    parser = argparser()
    args = parser.parse_args()
    sysex_console(args.port_number)


if __name__ == "__main__":
    main()
