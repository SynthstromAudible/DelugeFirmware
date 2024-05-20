#! /usr/bin/env python3
import sys
import time
import argparse
import rtmidi
import sys


def note(message):
    # Let's assume stdout is piped to a file, and put all
    # interactive notices to stderr.
    print(message, file=sys.stderr)


def argparser():
    parser = argparse.ArgumentParser(
        prog="sysex-logging",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Listen to console logs via MIDI",
        epilog="""\nusage example: dbt sysex-logging 123
                  start listening to console logs on MIDI port # 123""",
        exit_on_error=False,
    )
    parser.group = "Development"
    parser.add_argument(
        "ports",
        nargs="*",
        help="""MIDI output and input port numbers. Example: 123 312. If only one port
                number is given, same number is used for both. If no port numbers are
                given, first output and input ports whose names start with "Deluge",
                if any, are automatically used. Use 'dbt sysex-logging -h' to list
                available ports""",
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


def sysex_console(midiout, midiin):
    midiin.ignore_types(False, True, True)

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

    while True:
        msg_and_dt = midiin.get_message()
        if msg_and_dt:
            # unpack the msg and time tuple
            (msg, _) = msg_and_dt
            if (
                msg[0] == 0xF0
                and len(msg) > 8
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


def ensure_port(type, midi, port):
    if port is None:
        for i, p in enumerate(midi.get_ports()):
            if str(p).startswith("Deluge"):
                port = i
                break
    if port is None:
        note(
            f"Could not identify {type.strip()} port for Deluge. Aborting.",
        )
        exit(1)
    else:
        # Report ports to stderr so the logs remain separate.
        note(f"# MIDI {type} {port}: {midi.get_port_name(port)}")
    return port


def main():
    midiout = rtmidi.MidiOut()
    midiin = rtmidi.MidiIn()

    parser = argparser()
    ok = False
    try:
        args = parser.parse_args()
        outport, inport, *_ = args.ports + [None, None]

        if outport and not inport:
            inport = outport

        outport = ensure_port("output", midiout, outport)
        inport = ensure_port("input ", midiin, inport)

        midiout.open_port(outport)
        midiin.open_port(inport)
        ok = True
    except Exception as e:
        note(f"ERROR: {e}")
    finally:
        if not ok:
            note("\nAvailable MIDI output ports:")
            for i, p in enumerate(midiout.get_ports()):
                note(f"  {i}. {p}")
            note("\nAvailable MIDI input ports:")
            for i, p in enumerate(midiin.get_ports()):
                note(f"  {i}. {p}")
            exit(1)

    sysex_console(midiout, midiin)


if __name__ == "__main__":
    main()
