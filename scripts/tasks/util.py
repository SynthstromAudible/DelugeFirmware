from functools import partial
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


def find_cmd_with_fallback(cmd: str, fallback: str = None):
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
def progressbar(it, prefix: str, size: int = 60, out=sys.stdout):
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
    pool = multiprocessing.Pool()
    result = pool.map_async(func, it)
    while not result.ready():
        time.sleep(1)
    pool.close()
    pool.join()


class Counter(object):
    def __init__(self, initval=0):
        self.val = multiprocessing.RawValue("i", initval)
        self.lock = multiprocessing.Lock()

    def increment(self):
        with self.lock:
            self.val.value += 1

    @property
    def value(self):
        return self.val.value


## Multiprocessing

def init_globals(cntr):
    global counter
    counter = cntr


def call_and_increment(func, arg):
    func(arg)
    counter.increment()


def do_parallel_progressbar(func, it, prefix: str, size: int = 60, out=sys.stdout):
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

    counter = Counter()
    pool = multiprocessing.Pool(
        multiprocessing.cpu_count(), initializer=init_globals, initargs=(counter,)
    )
    result = pool.map_async(partial(call_and_increment, func), it)
    while not result.ready():
        show(counter.value)
        time.sleep(0.1)
    show(counter.value)
    pool.close()
    pool.join()
    print("", flush=True, file=out)
