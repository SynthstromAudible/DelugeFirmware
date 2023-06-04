from dbt.util import link_dir
from ansi.color import fg


def link_elf_dir_as_latest(env, elf_node):
    elf_dir = elf_node.Dir(".")
    latest_dir = env.Dir("#build-latest")
    print(fg.green(f"Linking {elf_dir} as latest built dir (./build-latest/)"))
    return link_dir(latest_dir.abspath, elf_dir.abspath, env["PLATFORM"] == "win32")


def should_gen_cdb_and_link_dir(env, requested_targets):
    return False