import multiprocessing
import subprocess
import sys
import shutil
import sysconfig
from pathlib import Path
import time


def run(args, redirect_input: bool = True, redirect_output: bool = True):
    # start child process
    process = subprocess.run(
        args,
        stdout=(sys.stdout if redirect_output else None),
        stdin=(sys.stdin if redirect_input else None),
        stderr=(sys.stderr if redirect_output else None),
    )

    return process.returncode


def find_cmd_with_fallback(cmd:str, fallback: str = None):
    if not fallback:
        fallback = cmd
    which_result = shutil.which(cmd)
    if which_result:
        return which_result
    else:
        return fallback


def absolute_path_str(path: str):
    return str(Path(path).absolute())


def run_get_output(args):
    return subprocess.run(args, stdout=subprocess.PIPE).stdout.decode().strip()


def convert_path_if_mingw(path: str):
    if sysconfig.get_platform().startswith("mingw"):
        path = run_get_output(["cygpath", "-w", path])
    return path


def get_git_root():
    git_root = run_get_output(["git", "rev-parse", "--show-toplevel"])
    git_root = convert_path_if_mingw(git_root)
    return Path(git_root)

# from https://stackoverflow.com/a/34482761
def progressbar(it, prefix:str, size:int=60, out=sys.stdout):
    count = len(it)

    def show(j):
        x = int(size * j / count)
        print(
            f"{prefix}[{u'#'*x}{('-'*(size-x))}] {j}/{count}",
            end="\r",
            file=out,
            flush=True,
        )

    show(0)
    for i, item in enumerate(it):
        yield item
        show(i + 1)
    print("", flush=True, file=out)

def do_parallel(func, it):
    p = multiprocessing.Process(target=func, args=it)
    p.start()
    p.join()
  
def do_parallel_progressbar(func, it, prefix:str, size:int=60, out=sys.stdout):
    count = len(it)
    def show(j):
        x = int(size * j / count)
        print(
            f"{prefix}[{u'#'*x}{('-'*(size-x))}] {j}/{count}",
            end="\r",
            file=out,
            flush=True,
        )
    show(0)    

    pool = multiprocessing.Pool()
    result = pool.map_async(func, it)
    while not result.ready():
        show(count - result._number_left)
        time.sleep(1)
    show(count)
    pool.close()
    pool.join()
    print("", flush=True, file=out)