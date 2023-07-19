#! /usr/bin/env python3
import argparse
import subprocess
import sys
import util
import os

def argparser():
    parser = argparse.ArgumentParser(
        prog="build",
        description="Run SCons in the current environment",
    )
    parser.group = "Building"
    return parser


def main():
    scons_args = sys.argv[1:]
    
    scons_args += ['--warn=target-not-built']

    if not os.environ.get('DBT_VERBOSE'):
        scons_args += ['-Q']

    os.chdir(util.get_git_root())
    subprocess.run(['scons'] + scons_args, env=os.environ)

if __name__ == "__main__":
    main()
