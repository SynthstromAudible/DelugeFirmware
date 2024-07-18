#! /usr/bin/env python3
import sys
import time
import argparse
import rtmidi
import sys
import util


def argparser():
    parser = argparse.ArgumentParser(
        prog="midi-clock",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Listens to MIDI clock from Deluge, outputs their time intervals.",
        epilog="""\nusage example: dbt midi-clock
                  Start listening to MIDI clock from Deluge.""",
        exit_on_error=False,
    )
    parser.group = "Development"
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        help="""MIDI input port number. If no port number is given, automatically
                tries to identify appropriate port for Deluge. Use 'dbt midi-clock -h'
                to list available ports""",
    )
    parser.add_argument(
        "-n",
        "--number",
        type=int,
        default=100,
        help="""Number of MIDI clocks to report. Default is 96 (one bar).""",
    )
    parser.add_argument(
        "-f",
        "--file",
        type=str,
        help="""File to save clock data to, by default prints to output instead.""",
    )
    return parser


def handle_clock_message(msg_and_dt, data):
    # unpack the msg and time tuple
    msg, dt = msg_and_dt
    # if clock...
    if msg[0] == 0xF8:
        print(dt)


def midi_clock(midiin, target, output):
    midiin.ignore_types(timing=False)
    delta = None
    # First clock
    while delta is None:
        msg_and_dt = midiin.get_message()
        if msg_and_dt:
            (msg, dt) = msg_and_dt
            if msg[0] == 0xF8:
                delta = 0
        else:
            time.sleep(0.01)
    n = 0
    # All subsequent clocks
    while n < target:
        msg_and_dt = midiin.get_message()
        if msg_and_dt:
            (msg, dt) = msg_and_dt
            if msg[0] == 0xF8:
                n = n + 1
                print("%.3f" % (delta + dt), flush=(n % 24 == 0), file=output)
                delta = 0
            else:
                delta = delta + dt
        else:
            time.sleep(0.01)


def main():
    midiin = rtmidi.MidiIn()
    target = None
    file = None

    parser = argparser()
    ok = False
    try:
        args = parser.parse_args()
        target = args.number
        file = args.file
        inport = util.ensure_midi_port(
            "input ", midiin, args.port, default_port_index=0
        )
        midiin.open_port(inport)
        ok = True
    except Exception as e:
        util.note(f"ERROR: {e}")
    finally:
        if not ok:
            util.report_available_midi_ports("input", midiin)
            exit(1)

    if file is None:
        midi_clock(midiin, target, sys.stdout)
    else:
        with open(file, "w") as output:
            midi_clock(midiin, target, output)


if __name__ == "__main__":
    main()
