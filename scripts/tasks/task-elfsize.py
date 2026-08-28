#! /usr/bin/env python3
import argparse
import os
import re
import sys
import zipfile
from dataclasses import dataclass
from urllib.request import urlretrieve

import util

# Sections whose address starts with these prefixes belong to internal RAM
# or external SDRAM respectively.
INTERNAL_ADDR_PREFIX = "20"
EXTERNAL_ADDR_PREFIX = "0c"

EXTERNAL_END_ADDR = 0x10000000
# Order in which categories are reported.
CATEGORIES = ["text", "rodata", "data", "bss"]

# Name of the section that marks the start of the internal heap (the free
# space between the end of statically allocated internal RAM and the start
# of the program stack), and the section that marks the start of the stack.
HEAP_SECTION_NAME = ".heap"
STACK_SECTION_NAME = ".program_stack"
# Name of the last section placed in external SDRAM before the free space
# used as the external heap.
EXTERNAL_BSS_SECTION_NAME = ".sdram_bss"

_SECTION_LINE = re.compile(
    r"^\s*\[\s*\d+\]\s+(?P<name>\S+)?\s+(?P<type>\S+)\s+"
    r"(?P<addr>[0-9a-fA-F]+)\s+(?P<off>[0-9a-fA-F]+)\s+(?P<size>[0-9a-fA-F]+)"
)


@dataclass
class Section:
    name: str
    addr: int
    size: int

    @property
    def location(self) -> str:
        addr_str = f"{self.addr:08x}"
        if addr_str.startswith(INTERNAL_ADDR_PREFIX):
            return "internal"
        if addr_str.startswith(EXTERNAL_ADDR_PREFIX):
            return "external"
        return "other"

    @property
    def category(self) -> str:
        name = self.name
        # e.g. .sdram_rodata / .rodata / .menu_rodata -> "rodata"
        if "rodata" in name:
            return "rodata"
        if name.endswith("text") or "text" in name:
            return "text"
        if name.endswith("data") or "data" in name:
            return "data"
        if "bss" in name:
            return "bss"
        return "other"


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="elfsize",
        description="Compare the internal/external (RAM/SDRAM) section sizes "
        "of two ELF files, using `arm-none-eabi-readelf -WS`",
    )
    parser.group = "Development"
    parser.add_argument(
        "-b",
        "--baseline",
        help="path of the 'old'/baseline ELF file (default - most recent release)",
        required=False,
        default="beta",
    )
    parser.add_argument("new_elf", help="path of the 'new' ELF file to compare")
    return parser


def parse_sections(elf_path: str) -> list[Section]:
    readelf = util.find_cmd_with_fallback("arm-none-eabi-readelf")
    output = util.run_get_output([readelf, "-WS", elf_path])

    sections = []
    for line in output.splitlines():
        m = _SECTION_LINE.match(line)
        if not m or not m.group("name"):
            continue
        sections.append(
            Section(
                name=m.group("name"),
                addr=int(m.group("addr"), 16),
                size=int(m.group("size"), 16),
            )
        )
    return sections


def summarize(sections: list[Section]) -> dict[tuple[str, str], int]:
    """Returns a dict of (location, category) -> total size in bytes."""
    totals: dict[tuple[str, str], int] = {}
    for section in sections:
        key = (section.location, section.category)
        totals[key] = totals.get(key, 0) + section.size
    return totals


def fmt_diff(old: int, new: int) -> str:
    diff = new - old
    return f"{diff:+,}"


def print_table(title: str, old_totals: dict, new_totals: dict, location: str):
    print(f"\n{title}:")
    header = f"  {'section':<10} {'old':>12} {'new':>12} {'diff':>12}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    old_loc_total = 0
    new_loc_total = 0
    for category in CATEGORIES:
        old_size = old_totals.get((location, category), 0)
        new_size = new_totals.get((location, category), 0)
        old_loc_total += old_size
        new_loc_total += new_size
        print(
            f"  {category:<10} {old_size:>12,} {new_size:>12,} "
            f"{fmt_diff(old_size, new_size):>12}"
        )
    print("  " + "-" * (len(header) - 2))
    print(
        f"  {'total':<10} {old_loc_total:>12,} {new_loc_total:>12,} "
        f"{fmt_diff(old_loc_total, new_loc_total):>12}"
    )


def find_section(sections: list[Section], name: str) -> Section | None:
    for section in sections:
        if section.name == name:
            return section
    return None


def compute_heap_sizes(sections: list[Section]) -> dict[str, int]:
    """Computes the actual internal/external heap sizes from linker layout.

    - internal heap: the free space between the end of the internal RAM
      (start of the `.heap` NOLOAD section) and the start of the program
      stack (`.program_stack`).
    - external heap: the free space between the end of the last statically
      allocated SDRAM section (`.sdram_bss`) and the end of external
      memory (0x10000000).
    """
    heap_sizes = {"internal": 0, "external": 0}

    heap_section = find_section(sections, HEAP_SECTION_NAME)
    stack_section = find_section(sections, STACK_SECTION_NAME)
    if heap_section is not None and stack_section is not None:
        heap_sizes["internal"] = stack_section.addr - heap_section.addr

    sdram_bss_section = find_section(sections, EXTERNAL_BSS_SECTION_NAME)
    if sdram_bss_section is not None:
        sdram_bss_end = sdram_bss_section.addr + sdram_bss_section.size
        heap_sizes["external"] = EXTERNAL_END_ADDR - sdram_bss_end

    return heap_sizes


def print_heap_table(old_heap_sizes: dict, new_heap_sizes: dict):
    print("\nHeap sizes:")
    header = f"  {'location':<10} {'old':>12} {'new':>12} {'diff':>12}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for location in ["internal", "external"]:
        old_size = old_heap_sizes.get(location, 0)
        new_size = new_heap_sizes.get(location, 0)
        print(
            f"  {location:<10} {old_size:>12,} {new_size:>12,} "
            f"{fmt_diff(old_size, new_size):>12}"
        )


def main() -> int:
    args = argparser().parse_args()
    print(f"args.baseline {args.baseline}")
    if args.baseline == "beta":
        print("downloading")
        path = "./scratch/beta_zip.zip"
        url = "https://github.com/SynthstromAudible/DelugeFirmware/releases/download/beta/beta.zip"
        os.makedirs("./scratch", exist_ok=True)
        urlretrieve(url, path)
        with zipfile.ZipFile(path) as zf:
            for file in zf.namelist():
                if file.endswith(".elf"):
                    old_elf = zf.extract(file, "./scratch")
    else:
        old_elf = args.baseline
    old_sections = parse_sections(old_elf)
    new_sections = parse_sections(args.new_elf)

    if not old_sections:
        print(f"Could not read any sections from '{args.old_elf}'", file=sys.stderr)
        return 1
    if not new_sections:
        print(f"Could not read any sections from '{args.new_elf}'", file=sys.stderr)
        return 1

    old_totals = summarize(old_sections)
    new_totals = summarize(new_sections)

    old_heap_sizes = compute_heap_sizes(old_sections)
    new_heap_sizes = compute_heap_sizes(new_sections)

    print(f"Comparing: {old_elf}  ->  {args.new_elf}")

    print_table("Internal (RAM, addr 0x20...)", old_totals, new_totals, "internal")
    print_table("External (SDRAM, addr 0x0C...)", old_totals, new_totals, "external")
    print_heap_table(old_heap_sizes, new_heap_sizes)

    return 0


if __name__ == "__main__":
    sys.exit(main())
