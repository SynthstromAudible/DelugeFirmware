from dmui.dsl import *
from dmui.visitor import CppEmitter, DocEmitter
import argparse
import json
import sys

lpf_freq = Menu(
    "filter::LPFFreq",
    "lpfFreqMenu",
    ["{name}", "{title}", "params::LOCAL_LPF_FREQ"],
    "STRING_FOR_LPF_FREQUENCY",
    "filter/lpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
)

lpf_res = Menu(
    "patched_param::IntegerNonFM",
    "lpfResMenu",
    ["{name}", "{title}", "params::LOCAL_LPF_RESONANCE"],
    "STRING_FOR_LPF_RESONANCE",
    "filter/lpf/resonance.md",
    name="STRING_FOR_RESONANCE",
)

lpf_mode = Menu(
    "filter::LPFMode",
    "lpfModeMenu",
    ["{name}", "{title}"],
    "STRING_FOR_LPF_MODE",
    "filter/lpf/resonance.md",
    name="STRING_FOR_MODE",
)

lpf_morph = MultiModeMenu(
    "filter::FilterMorph",
    "lpfMorphMenu",
    ["{title}", "params::LOCAL_LPF_MORPH", "false"],
    "STRING_FOR_MORPH",
    [
        MultiModeMenuMode(
            "STRING_FOR_DRIVE", "LPF is in a ladder mode", "filter/lpf/drive.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "LPF is in an SVF mode", "filter/lpf/morph.md"
        ),
    ],
)

lpf_menu = Submenu(
    "submenu::Filter",
    "lpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "STRING_FOR_LPF",
    "filter/lpf/index.md",
    [lpf_freq, lpf_res, lpf_mode, lpf_morph],
    name="STRING_FOR_LPF",
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
            result = lpf_menu.visit(emitter)
            print(json.dumps(result))

    if args.cpp_output is not None:
        with open(args.cpp_output, "w") as outf:
            emitter = CppEmitter(outf)
            result = lpf_menu.visit(emitter)
            emitter.finalize()


if __name__ == "__main__":
    main()
