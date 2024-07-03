import os
import clang.cindex
import argparse
import subprocess
from iterfzf import iterfzf


FN_BLACKLIST = [
    "graphicsRoutine"
]
tags_file = "tags"  # Replace with the actual path to your tags file

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="annotate",
        description="Adds print statements everywhere"
    )
    parser.add_argument('action', choices=['add', 'rm'], help="The action to perform")
    parser.add_argument(
        "-p", "--path", help="the directory to process"
    )
    parser.add_argument(
        "-c", "--comment", help="A comment added to the annotation to help with search/removal", default="DBT:ANNOTATE"
    )
    parser.add_argument(
        "-m", "--max", help="Maximum number of files to annotate"
    )
    parser.add_argument(
        "-t", "--template", help="A template for the log statement", default='D_PRINTLN("{function}");'
    )

    return parser

def remove_specific_lines(file_path, text_to_remove):
    with open(file_path, 'r') as file:
        lines = file.readlines()
        filtered_lines = [line for line in lines if text_to_remove not in line]
        if len(filtered_lines) < len(lines):
            with open(file_path, 'w') as file:
                file.writelines(filtered_lines)
            print(f"Removed {len(lines) - len(filtered_lines)} lines from {file_path}")
            return

def remove_annotations(args):
    for root, dirs, files in os.walk(args.path):
        for file in files:
            if file.endswith('.cpp'):
                remove_specific_lines(os.path.join(root, file), args.comment)

def is_function_definition(node):
    """
    Check if the node is a function definition.
    """
    if node.kind != clang.cindex.CursorKind.CXX_METHOD:
        return False
    for child in node.get_children():
        if child.kind == clang.cindex.CursorKind.COMPOUND_STMT:
            return True
    return False



def ensure_tags_exist():
    result = subprocess.run(["ctags", "--version"])
    ctags_binary = "ctags"
    if result.returncode != 0:
        def iter_ctags_binaries():
            which_ctags_result = subprocess.run(["which", "-a", "ctags"], capture_output=True)
            for line in which_ctags_result.stdout.decode('utf-8').splitlines():
                yield line
        print("Default ctags is erroring out on `ctags --version`. GNU standard ctags required. (brew install universal-ctags on MacOSX).")
        ctags_binary = iterfzf(iter_ctags_binaries(), prompt = 'Please choose the appropriate ctags path: ')

    ctags_invocation = [ctags_binary, "-R", "-x", "--c++-types=f", "--extras=q", "--format=1", 
                    "src/deluge/gui/views" ,
                    "src/deluge/deluge.cpp",
                    "src/deluge/model/action",
                    "src/deluge/model/clip", 
                    "src/deluge/model/consequence",
                    "src/deluge/model/drum",
                    "src/deluge/model/instrument"]
    print(f"invoking {' '.join(ctags_invocation)}")
    result = subprocess.run(ctags_invocation, capture_output=True)
    if result.returncode == 0:
        with open(tags_file, "w+") as tags_file_pointer:
            tags_file_pointer.write(result.stdout.decode('utf-8'))
    else:
        print(f"Failed to invoke {" ".join(ctags_invocation)}")
        exit(1)

def parse_ctags_output(tags_file):
    function_map = {}
    ensure_tags_exist()
    with open(tags_file, 'r') as file:
        for line in file:
            parts = line.split()
            if len(parts) >= 3:
                function_name, line_number, file_path = parts[0], int(parts[1]), parts[2]

                # Check if the function (namespaced or not) is already in the map
                already_included = any(fn in function_name or function_name in fn for fn, _ in function_map.get(file_path, []))
                
                if file_path not in function_map:
                    function_map[file_path] = []
                
                blacklisted = False
                for blacklisted_fn in FN_BLACKLIST:
                    if blacklisted_fn in function_name:
                        blacklisted = True
                        break
                # Add the function if it's not a non-namespaced version of an existing namespaced function
                if not already_included and not blacklisted:
                    function_map[file_path].append((function_name, line_number))
    return function_map

def add_annotations(args):
    function_map = parse_ctags_output(tags_file)

    processed_files = 0
    for root, dirs, files in os.walk(args.path, followlinks=False):
        for file in files:
            if file.endswith('.cpp'):
                file_path = os.path.join(root, file)
                if file_path in function_map:
                    functions = sorted(function_map[file_path], key=lambda x: x[1], reverse=True)
                    if args.max and processed_files > int(args.max):
                        break
                    annotate_file(file_path, functions, args)
                    processed_files += 1

def annotate_file(file_path, functions, args):
    with open(file_path, 'r') as file:
        lines = file.readlines()

    for function_name, line_number in functions:
        # Search for the opening curly brace from the line number
        insertion_point = line_number
        for i in range(line_number - 1, min(line_number + 5, len(lines))):  # Check next few lines for the opening brace
            if '{' in lines[i]:
                insertion_point = i + 1  # Insert after the opening brace
                break

        # basename without extension
        basename = os.path.basename(file_path)
        debug_line = args.template.replace('{function}', f"{function_name}").replace('{file}', basename)
        debug_line += f" // {function_name} {args.comment}\n"

        if debug_line.strip() not in lines[insertion_point].strip():  # Avoid duplicates
            lines.insert(insertion_point, debug_line)

        print("".join(lines))
    # Uncomment to write changes
    with open(file_path, 'w') as file:
        file.writelines(lines)

# Rest of the script remains the same...


def main() -> int:
    args = argparser().parse_args()
    if (args.action == 'rm'):
        remove_annotations(args)
    else:
        add_annotations(args)
    return 0
