import time
import binascii
import argparse
import util
import itertools
import rtmidi
import os


def argparser():
    parser = argparse.ArgumentParser(
        prog="loadfw",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Send firmware to Deluge via MIDI",
        epilog="""\nusage example: dbt loadfw relwithdebinfo
                  Send build/relwithdebinfo/deluge.bin to MIDI port identified with deluge,
                  using hex key stored in .deluge_hex_key.""",
        exit_on_error=False,
    )
    parser.group = "Development"
    parser.add_argument(
        "build",
        help="""Path to firmware binary file, or build type.
                Examples:
                   ./build/Debug/deluge.bin # Sends the specified file
                   release                  # Sends ./build/release/deluge.bin""",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        help="""MIDI output port. Default is first port with device called Deluge.
                Use dbt loadfw -h to list available ports.""",
    )
    parser.add_argument(
        "-k",
        "--key",
        help="""Deluge hex key. 8 digit key in SETTINGS > COMMUNITY FEATURES
                > ALLOW INSECURE DEVELOP SYSEX MESSAGES. Defaults to string
                stored in .deluge_hex_key, or an interactive request for the key.""",
    )
    parser.add_argument(
        "-d",
        "--delay",
        default=2,
        type=int,
        help="""Delay in ms between SysEx packets. Default is 2.
                Increase in case of checksum errors.""",
    )
    parser.add_argument(
        "-o",
        "--outfile",
        help="Output SysEx data to file specified, instead of sending it over MIDI.",
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

    data[7:12] = pack_8_to_7_bits(handshake.to_bytes(4, byteorder="little"), 5)

    data[12] = segment_number & 0x7F  # Position Lower 7 bits
    data[13] = (segment_number >> 7) & 0x7F  # Position Upper 7 bits

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


def make_sysex_messages(binary, handshake):
    checksum = binascii.crc32(binary)
    length = len(binary)
    messages = []
    for i, segment in enumerate(itertools.batched(binary, 512)):
        messages.append(deluge_sysex_message_firmware_segment(i, segment, handshake))
    messages.append(deluge_sysex_message_firmware_load(checksum, length, handshake))
    return messages


def load_fw(output, handshake, file, delay_ms=2, output_to_file=False):
    with open(file, "rb") as f:
        binary = f.read()

    sysex_data = make_sysex_messages(binary, handshake)

    if output_to_file:
        with open(output, "wb") as f:
            text = f"Writing SysEx File '{f.name}': "
            for msg in util.progressbar(sysex_data, text):
                f.write(msg)

    else:
        with output:
            for msg in util.progressbar(sysex_data, "Firmware Upload: "):
                output.send_message(msg)
                time.sleep(0.001 * delay_ms)


def try_read_hex_key():
    name = ".deluge_hex_key"
    try:
        with open(name, "r") as f:
            return f.read().strip()
    except:
        hex_key = input(
            "Enter hex key from COMMUNITY FEATURES > ALLOW INSECURE DEVELOP SYSEX MESSAGES: "
        )
        if "Y" == input("Save hex key (Y/N)? ").upper():
            with open(name, "w") as f:
                f.write(hex_key)
        return hex_key


def find_binary(build):
    if os.path.isfile(build):
        return build
    id = build.upper()
    if id == "RELEASE":
        return "build/Release/deluge.bin"
    if id == "DBEUG":
        return "build/Debug/deluge.bin"
    if id == "RELWITHDEBINFO":
        return "build/RelWithDebInfo/deluge.bin"
    raise RuntimeError(
        f"""Unknown build type: {build}. Should be either
                           path to a binary, or one of: release, debug, or
                           relwithdebinfo."""
    )


def main():
    hex_key = None
    binary = None
    delay = None
    output = None
    output_to_file = True
    parser = argparser()
    ok = False
    try:
        args = parser.parse_args()
        binary = find_binary(args.build)
        hex_key = args.key or try_read_hex_key()
        output = args.outfile
        if output is None:
            output_to_file = False
            output = rtmidi.MidiOut()
            port = util.ensure_midi_port("output", output, args.port)
            output.open_port(port)
        delay = args.delay
        ok = True
    except Exception as e:
        util.note(f"ERROR: {e}")
    finally:
        if not ok:
            util.report_available_midi_ports("output", rtmidi.MidiOut())
            exit(1)

    load_fw(output, int(hex_key, 16), binary, delay, output_to_file)


if __name__ == "__main__":
    main()
