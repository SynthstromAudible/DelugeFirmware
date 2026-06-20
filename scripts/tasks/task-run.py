#! /usr/bin/env python3
import argparse
import binascii
import glob
import importlib
import os
import shutil
import subprocess
import sys

import util

# Re-use the firmware build's config name map (lowercase alias -> CMake dir).
BUILD_CONFIGS = importlib.import_module("task-build").BUILD_CONFIGS

# Dev-mode USB upload wire-frame (see deluge-sdk cargo-deluge frame.rs):
#   magic | version u8 | flags u8 | len u32 LE | crc32 u32 LE | <elf>
FRAME_MAGIC = b"DLUP"
FRAME_VERSION = 1

# The Deluge dev-upload CDC port identifiers (cargo-deluge main.rs).
DELUGE_VID = 0x16D0
DELUGE_PID = 0x0EDA
UPLOAD_PRODUCT = "Dev Upload"

# The loader stages the whole ELF in a scratch window before parsing it and
# rejects anything larger (devupload::SCRATCH_LEN). Catch oversize locally so we
# don't ship megabytes only for the device to drop it.
MAX_UPLOAD_BYTES = 0x00C0_0000  # 12 MiB

BAUD = 115200
CHUNK = 4096


def argparser():
    parser = argparse.ArgumentParser(
        prog="run",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Build the firmware and upload it to a Deluge over USB (dev mode)",
        epilog="""\nusage example: dbt run release
                  Build build/Release/deluge.elf and push it to a Deluge sitting
                  on the app-loader boot menu with DEV MODE: ON.""",
    )
    parser.group = "Development"
    parser.add_argument(
        "config",
        nargs="?",
        default="debug",
        choices=list(BUILD_CONFIGS.keys()) + list(BUILD_CONFIGS.values()),
        help="Firmware build configuration to build and upload (default: debug).",
    )
    parser.add_argument(
        "-p",
        "--port",
        help="""Serial device path of the Deluge dev-upload port.
                Default is auto-discovered by USB VID/PID.""",
    )
    parser.add_argument(
        "-f",
        "--file",
        help="Upload this prebuilt ELF instead of building (config is ignored).",
    )
    parser.add_argument(
        "--no-strip",
        action="store_true",
        help="Upload the unstripped ELF (larger transfer; off by default).",
    )
    return parser


def build_frame(elf_bytes):
    frame = bytearray()
    frame += FRAME_MAGIC
    frame.append(FRAME_VERSION)
    frame.append(0)  # flags (reserved)
    frame += len(elf_bytes).to_bytes(4, "little")
    frame += (binascii.crc32(elf_bytes) & 0xFFFFFFFF).to_bytes(4, "little")
    frame += elf_bytes
    return bytes(frame)


def find_elf(config):
    cmake_config = BUILD_CONFIGS.get(config, config)
    pattern = os.path.join("build", cmake_config, "deluge*.elf")
    matches = sorted(glob.glob(pattern))
    if not matches:
        raise RuntimeError(
            f"No firmware ELF found at {pattern}. Did the build succeed?"
        )
    if len(matches) > 1:
        joined = "\n  ".join(matches)
        raise RuntimeError(
            f"Multiple ELFs matched {pattern}; pass one with --file:\n  {joined}"
        )
    return matches[0]


def find_objcopy():
    for cand in ("arm-none-eabi-objcopy", os.environ.get("CMAKE_OBJCOPY")):
        if cand and shutil.which(cand):
            return shutil.which(cand)
    return None


def strip_for_upload(elf):
    # The loader reads only program headers + entry point, so debug/symbol
    # sections just bloat (and can overflow) the transfer. Mirror cargo-deluge's
    # run.rs::strip_for_upload: strip a sibling copy, fall back to the original.
    objcopy = find_objcopy()
    if not objcopy:
        util.note(
            "WARNING: arm-none-eabi-objcopy not found; sending the unstripped ELF."
        )
        return elf
    out = elf + ".stripped"
    subprocess.check_call([objcopy, "--strip-all", elf, out])
    before = os.path.getsize(elf)
    after = os.path.getsize(out)
    pct = (before - after) * 100 // max(before, 1)
    print(f"Stripped {before} -> {after} bytes ({pct}% smaller)")
    return out


def discover_upload_port(list_ports):
    deluge = []
    for p in list_ports.comports():
        if p.vid == DELUGE_VID and p.pid == DELUGE_PID:
            deluge.append((p.device, p.product))

    # Prefer a product-string match for the upload listener.
    for device, product in deluge:
        if product and UPLOAD_PRODUCT in product:
            return device

    # Some platforms don't expose product strings: a lone Deluge port is enough.
    if len(deluge) == 1:
        return deluge[0][0]
    if not deluge:
        raise RuntimeError(
            "No Deluge serial port found. Put the unit on the boot menu with "
            "DEV MODE: ON, connect it over USB, or pass --port <path>."
        )
    listing = "\n  ".join(f"{d}  ({p or '?'})" for d, p in deluge)
    raise RuntimeError(
        f"Multiple Deluge serial ports found; pick one with --port:\n  {listing}"
    )


def upload(serial_mod, port_path, frame):
    # USB bulk flow-control naturally paces the write against the device drain.
    chunks = [frame[i : i + CHUNK] for i in range(0, len(frame), CHUNK)]
    with serial_mod.Serial(port_path, BAUD, timeout=5) as port:
        for chunk in util.progressbar(chunks, "Firmware Upload: "):
            port.write(chunk)
        port.flush()


def main():
    try:
        import serial
        import serial.tools.list_ports as list_ports
    except ImportError:
        util.install_pyserial()
        import serial
        import serial.tools.list_ports as list_ports

    args = argparser().parse_args()

    os.chdir(util.get_git_root())

    if args.file:
        elf = args.file
        if not os.path.isfile(elf):
            util.note(f"ERROR: {elf} does not exist.")
            return 1
    else:
        result = importlib.import_module("task-build").main([args.config])
        if result != 0:
            return result
        try:
            elf = find_elf(args.config)
        except RuntimeError as e:
            util.note(f"ERROR: {e}")
            return 1

    upload_elf = elf if args.no_strip else strip_for_upload(elf)

    with open(upload_elf, "rb") as f:
        elf_bytes = f.read()

    if len(elf_bytes) > MAX_UPLOAD_BYTES:
        util.note(
            f"ERROR: image is {len(elf_bytes)} bytes, larger than the loader's "
            f"{MAX_UPLOAD_BYTES}-byte upload window. Build `release`, or trim the firmware."
        )
        return 1

    frame = build_frame(elf_bytes)

    try:
        port_path = args.port or discover_upload_port(list_ports)
    except RuntimeError as e:
        util.note(f"ERROR: {e}")
        return 1

    print(f"Uploading {upload_elf} ({len(elf_bytes)} bytes) to {port_path}")
    upload(serial, port_path, frame)
    print("\nUploaded — the Deluge is loading and launching it from RAM.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
