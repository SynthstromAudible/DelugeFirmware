from dmui.dsl import *
from dmui.visitor import CppEmitter, DocEmitter
import dmenus
import argparse
import json
import sys

top_level_menus = (
    [
        dmenus.filter.sound_filters,
        dmenus.compressor.menu,
        dmenus.unison.menu,
    ]
    + dmenus.oscillator.menus
    + dmenus.envelope.menus
    + dmenus.lfo.menus
)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-d", "--doc-output", required=False)
    parser.add_argument("-c", "--cpp-output", required=False)

    return parser.parse_args()


def main():
    args = parse_args()

    if args.doc_output is not None:
        with open(args.doc_output, "w") as outf:
            emitter = DocEmitter()
            result = [menu.visit(emitter) for menu in top_level_menus]
            outf.write(json.dumps(result))

    if args.cpp_output is not None:
        with open(args.cpp_output, "w") as outf:
            emitter = CppEmitter(outf)
            for menu in top_level_menus:
                menu.visit(emitter)
            emitter.finalize()


if __name__ == "__main__":
    main()
