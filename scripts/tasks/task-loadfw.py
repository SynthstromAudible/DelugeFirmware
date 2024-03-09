import time
import binascii
import argparse
import util
import rtmidi


def argparser():
    midiout = rtmidi.MidiOut()
    available_ports = midiout.get_ports()
    s = "\n\nusage example: dbt loadfw 123 abcd1243 /path/deluge.bin"
    s += "\n\nloads /path/deluge.bin to the deluge with key abcd1243, connected on MIDI port # 123"
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
    parser.add_argument(
        "port_number",
        help="MIDI port number (example: 123). Use 'dbt loadfw -h' to list available ports.",
    )
    parser.add_argument("hex_key", help="8-digit Deluge Hex Key (example 1234abcd)")
    parser.add_argument(
        "firmware_file",
        help="Path to firmware binary file (example ./build/Debug/deluge.bin)",
    )
    parser.add_argument(
        "-d",
        default=2,
        type=int,
        help="Delay in ms between SysEx packets. Default is 2. Increase in case of checksum errors.",
    )
    parser.add_argument(
        "-o",
        action="store_true",
        help="Output SysEx data to file. Specify filename instead of port number.",
    )

    return parser


def pack_8_to_7_bits(src, dstsize):
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


def deluge_sysex_message_firmware_segment(segment_number, segment_data, handshake):
    data = bytearray(601)

    data[0] = 0xF0  # Sysex Start

    data[1] = 0x00  # Deluge SysEx ID
    data[2] = 0x21  # Deluge SysEx ID
    data[3] = 0x7B  # Deluge SysEx ID
    data[4] = 0x01  # Deluge SysEx ID

    data[5] = 3  # Deluge Debug
    data[6] = 1  # Deluge Send Firmware

    data[7:12] = pack_8_to_7_bits(
        handshake.to_bytes(4, byteorder="little"), 5
    )  # Packed Handshake

    data[12] = segment_number & 0x7F  # Position Low
    data[13] = (segment_number >> 7) & 0x7F  # Position High

    data[14:-1] = pack_8_to_7_bits(segment_data, 586)  # Packed Data

    data[-1] = 0xF7  # Sysex End

    return data


def deluge_sysex_message_firmware_load(checksum, length, handshake):
    data = bytearray(22)

    data[0] = 0xF0  # Sysex Start

    data[1] = 0x00  # Deluge SysEx ID
    data[2] = 0x21  # Deluge SysEx ID
    data[3] = 0x7B  # Deluge SysEx ID
    data[4] = 0x01  # Deluge SysEx ID

    data[5] = 3  # Deluge Debug
    data[6] = 2  # Deluge Load Firmware

    data[7:21] = pack_8_to_7_bits(
        handshake.to_bytes(4, byteorder="little")
        + length.to_bytes(4, byteorder="little")
        + checksum.to_bytes(4, byteorder="little"),
        14,
    )

    data[21] = 0xF7  # Sysex End

    return data


def binary_to_indexed_512_byte_chunks(binary):
    return list(
        enumerate((binary[pos : pos + 512] for pos in range(0, len(binary), 512)))
    )


def load_fw(port, handshake, file, delay_ms=2):

    with open(file, "rb") as f:
        binary = f.read()
    checksum = binascii.crc32(binary)
    length = len(binary)

    midiout = rtmidi.MidiOut()
    midiout.open_port(port)
    with midiout:

        for i, segment in util.progressbar(
            binary_to_indexed_512_byte_chunks(binary), "Firmware Upload "
        ):

            msg = deluge_sysex_message_firmware_segment(i, segment, handshake)
            midiout.send_message(msg)
            time.sleep(0.001 * delay_ms)

        msg = deluge_sysex_message_firmware_load(checksum, length, handshake)
        midiout.send_message(msg)


def main():
    parser = argparser()
    args = parser.parse_args()
    load_fw(args.port_number, int(args.hex_key, 16), args.firmware_file, args.d)


if __name__ == "__main__":
    main()
