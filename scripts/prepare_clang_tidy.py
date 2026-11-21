"""Merge firmware/host databases without applying ARM includes to host tests."""

import argparse
import functools
import json
import shlex
import subprocess
from pathlib import Path


@functools.cache
def system_includes(compiler, language, target_flags, resource_dir):
    result = subprocess.run(
        [compiler, *target_flags, "-E", "-x", language, "-", "-v"],
        input="",
        text=True,
        capture_output=True,
        check=True,
    )
    search = result.stderr.split("#include <...> search starts here:", 1)[1]
    search = search.split("End of search list.", 1)[0]
    paths = [
        str(Path(line.strip()).resolve())
        for line in search.splitlines()
        if line.strip() and "(framework directory)" not in line
    ]
    # Use the selected compiler's C++ library, but Clang's builtin/intrinsic
    # headers. GCC's arm_acle.h uses GCC-only builtins that Clang cannot parse.
    cxx = [p for p in paths if "/include/c++/" in p]
    other = [
        p
        for p in paths
        if p not in cxx and "/lib/gcc/" not in p and "/lib/clang/" not in p
    ]
    return cxx + [str(Path(resource_dir) / "include")] + other


def prepare(database, firmware, resource_dir):
    for entry in json.loads(database.read_text()):
        args = entry.get("arguments") or shlex.split(entry["command"])
        target_flags = tuple(
            a
            for a in args
            if a == "-m32" or a.startswith(("-mcpu=", "-mfpu=", "-mfloat-abi="))
        )
        language = "c" if Path(entry["file"]).suffix == ".c" else "c++"
        includes = system_includes(args[0], language, target_flags, resource_dir)
        extra = ["--no-standard-includes"]
        if firmware:
            extra.append("--target=arm-none-eabi")
        for path in includes:
            extra.extend(["-isystem", path])
        unsupported = {
            "-mthumb-interwork",
            "-fdevirtualize-at-ltrans",
            "-fuse-linker-plugin",
        }
        entry["arguments"] = [
            args[0],
            *extra,
            *(a for a in args[1:] if a not in unsupported),
        ]
        entry.pop("command", None)
        yield entry


def add_header_commands(commands):
    # The annotation action also invokes clang-tidy directly on changed headers.
    # Give sibling headers an explicit language/command rather than letting
    # tooling infer one (which can place ExtraArgs after a '--' separator).
    headers = {}
    for command in commands:
        source = Path(command["file"])
        if source.suffix not in {".c", ".cpp", ".cc", ".cxx"}:
            continue
        for extension in (".h", ".hpp"):
            header = source.with_suffix(extension)
            if header.is_file():
                headers.setdefault(header, command)
    firmware_cpp = next(
        c
        for c in commands
        if "--target=arm-none-eabi" in c["arguments"]
        and Path(c["file"]).suffix == ".cpp"
    )
    for header in Path("src").glob("*.h"):
        headers.setdefault(header.resolve(), firmware_cpp)
    for header, source_command in headers.items():
        command = dict(source_command)
        args = [a for a in command["arguments"] if a != command["file"]]
        language = "c" if Path(command["file"]).suffix == ".c" else "c++"
        args.extend(["-x", language + "-header", str(header)])
        command["file"] = str(header)
        command["arguments"] = args
        yield command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware-db", type=Path, required=True)
    parser.add_argument("--host-db", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clang", default="clang")
    args = parser.parse_args()
    resource_dir = subprocess.check_output(
        [args.clang, "-print-resource-dir"], text=True
    ).strip()
    commands = list(prepare(args.firmware_db, True, resource_dir))
    commands.extend(prepare(args.host_db, False, resource_dir))
    commands.extend(list(add_header_commands(commands)))
    commands = list({c["file"]: c for c in reversed(commands)}.values())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(commands, indent=2) + "\n")


if __name__ == "__main__":
    main()
