#! /usr/bin/env python3
import argparse
import subprocess
import sys
import util
import os

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="build",
        description="Build in the current environment",
    )
    parser.group = "Building"
    parser.add_argument(
        "target",
        default="all",
        const="all",
        nargs="?",
        choices=["7seg", "oled", "all", 'clean'],
    )    
    parser.add_argument(
        "config",
        default="debug",
        const="debug",
        nargs="?",        
        choices=["release", "debug", "relwithdebinfo"],
    )    
    return parser


def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()
    if args.target in ['7seg', 'oled']:
        target = "Deluge" + args.target.upper()
    else:
        target = args.target

    os.chdir(util.get_git_root())

    configure_args = [
        "-B",
        "build",
        "-G",
        "Ninja Multi-Config",
        "-DCMAKE_CROSS_CONFIGS:STRING=all",
        "-DCMAKE_DEFAULT_CONFIGS=Debug;Release",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE",
    ]
    if not os.path.exists("build"):
        subprocess.run(['cmake'] + configure_args, env=os.environ)
        
    build_args = [
        "--build",
        "build",
        "--config", f"{args.config.title()}",
        "--target",
        target,
    ]
    result = subprocess.run(['cmake'] + build_args + unknown_args, env=os.environ)
    return result.returncode

if __name__ == "__main__":
    main()
