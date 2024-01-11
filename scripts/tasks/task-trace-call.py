from scripts.dbt.util import path_as_posix, platform_arch
import argparse
from pathlib import Path
import shutil
import os
import sys
import subprocess
import re
import json

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="trace-call",
        description="Trace problematic function calls (E.g. _malloc_r)",
    )
    parser.add_argument(
        "-f", "--function", help="Disable ninja-build status line", action="store_true", default="_malloc_r"
    )
    parser.add_argument(
        "-t", "--target", help="Path to a .elf file", action="store_true", default=path_as_posix("build/Release/deluge.bin")
    )
    parser.group = "Debugging"
    return parser




def trace_invocation(invocation, from_search, lines):
    i = from_search
    reverse_i = from_search
    while (i - reverse_i) < 400:
        call_match = re.search(r"([0-9a-f]+) <(.*?)>:$", lines[reverse_i])
        if call_match is not None:
            caller_address, caller_name = call_match.groups()
            return caller_address, caller_name
        reverse_i = reverse_i - 1
    return None, None

def trace_invokers(address, caller_name, lines, chain=[]):
    if len(chain) > 5:
        return []
    
    invocations = []
    invoker_regex = rf"^([a-z0-9]+):.*0x{address} <{caller_name}>"
    for [x, line] in enumerate(lines):
        result = re.search(invoker_regex, line)
        if result is not None:
            invocation = result.group()
            caller_address, caller_name = trace_invocation(invocation, x, lines)
            subs = trace_invokers(caller_address, caller_name, lines, chain + [caller_address])
            invocations.append({
                "invocation": invocation,
                "caller_address": caller_address,
                "caller_name": caller_name,
                "subs": subs
            })
    return invocations

def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()
    
    result = subprocess.run(
        [path_as_posix(f"toolchain/{platform_arch()}/arm-none-eabi-gcc/bin/arm-none-eabi-objdump"), "-Cd", args.target],
        capture_output=True,
        text=True
    )
    dumped = result.stdout
    binary_name = os.path.basename(args.target).replace('.elf','')
    with open(path_as_posix(f"build/Release/{binary_name}.objdump"), "w") as f:
        f.write(dumped)
    dumped_lines = dumped.splitlines()
    invocations = trace_invokers("[a-z0-9]+", args.function, dumped_lines)
    if len(invocations) > 0:
        print(json.dumps(invocations, indent=2), file=sys.stderr)
        exit(1)
    else:
        print(f"No instances of {args.function} detected")
    exit(0)

if __name__ == "__main__":
    main()
