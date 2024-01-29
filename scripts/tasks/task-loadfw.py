import sys
import time
import binascii
import argparse
import util
import rtmidi


def argparser():
    midiout = rtmidi.MidiOut()
    available_ports = midiout.get_ports()
    s = "\nusage example: dbt loadfw 3 abcd1243 /path/deluge.bin"
    s += "\n\nloads /path/deluge.bin to the deluge with key abcd1243, connected on MIDI port 3"
    s += "\n\nAvailable MIDI ports:\n\n"
    for i, p in enumerate(available_ports):
        s += "Port # " + str(i) + " : " + str(p) + "\n"

    parser = argparse.ArgumentParser(
        prog="loadfw",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Send firmware to Deluge via MIDI",
        epilog=s,
        exit_on_error=True,
    )
    parser.group = "Development"
    parser.add_argument("port_number", help="MIDI port number (example: 0)", type=int)
    parser.add_argument("hex_key", help="8-digit Deluge Hex Key (example 1234abcd)")
    parser.add_argument(
        "firmware_file",
        help="Path to firmware binary file (example ./build/Debug/deluge.bin)",
    )
    parser.add_argument(
        "-d",
        default=1,
        type=int,
        help="Delay in milliseconds between SysEx packets. Default is 1. Increase in case of checksum errors.",
    )
    return parser


def pack87(src, dstsize):
    packets = (len(src) + 6) // 7
    dst = bytearray(dstsize)
    for i in range(packets):
        ipos = 7 * i
        opos = 8 * i
        for j in range(7):
            if ipos + j < len(src):
                temp = src[ipos + j] & 0x7F
                dst[opos + 1 + j] = temp
                if src[ipos + j] & 0x80:
                    dst[opos] |= 1 << j
    return dst


def load_fw(port, handshake, file, delay_ms=1):
    with open(file, "rb") as f:
        binary = f.read()
    checksum = binascii.crc32(binary)
    midiout = rtmidi.MidiOut()
    midiout.open_port(port)
    with midiout:
        data = bytearray(598)
        for i, segment in util.progressbar(
            list(
                enumerate(
                    (binary[pos : pos + 512] for pos in range(0, len(binary), 512))
                )
            ),
            "Firmware Upload ",
        ):
            data[0] = 0xF0  # Sysex Start
            data[1] = 0x7D  # Deluge
            data[2] = 3  # Debug
            data[3] = 1  # Send Firmware
            data[4:9] = pack87(handshake.to_bytes(4, byteorder="little"), 5)
            data[9] = i & 0x7F  # Position Low
            data[10] = (i >> 7) & 0x7F  # Position High
            data[11:-1] = pack87(segment, 586)
            data[-1] = 0xF7  # Sysex End
            midiout.send_message(data)
            time.sleep(0.001 * delay_ms)
        data = data[:19]
        data[3] = 2  # Load Firmware
        data[4:18] = pack87(
            handshake.to_bytes(4, byteorder="little")
            + len(binary).to_bytes(4, byteorder="little")
            + checksum.to_bytes(4, byteorder="little"),
            14,
        )
        data[18] = 0xF7
        midiout.send_message(data)


def main():
    parser = argparser()
    args = parser.parse_args()
    load_fw(args.port_number, int(args.hex_key, 16), args.firmware_file, args.d)


if __name__ == "__main__":
    main()
