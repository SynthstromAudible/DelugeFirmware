import sys
import os
from datetime import datetime
from git import Repo, InvalidGitRepositoryError
from SCons.Script import GetLaunchDir
from SCons.Action import Action
from dbt.exit_codes import ExitCodes
from dbt.util import vcheck, vprint


COMMUNITY_REMOTE_URL_PARTS = [
    ":SynthstromAudible/DelugeFirmware",
    "/SynthstromAudible/DelugeFirmware",
]


def _prepare_git_info(env):
    """Add git-related data to the environment."""
    try:
        repo = Repo(GetLaunchDir())
    except InvalidGitRepositoryError:
        print("DBT must be run from within a valid Git repository.")
        Exit(ExitCodes.INVALID_GIT_REPOSITORY)

    if repo.head.is_detached:
        env["GIT_BRANCH_NAME"] = "{}-detached".format(repo.commit().hexsha[0:7])
    else:
        env["GIT_BRANCH_NAME"] = repo.active_branch
    env["GIT_IS_COMMUNITY"] = False  # TODO: better detection through remotes
    env["GIT_IS_OFFICIAL"] = False  # TODO: better detection through remotes
    env["GIT_COMMIT"] = repo.commit(repo.head).hexsha
    env["GIT_DIRTY"] = repo.is_dirty()


def _get_build_type(env):
    build_label_parts = env["BUILD_LABEL"].split("-")
    if len(build_label_parts) > 3:
        return build_label_parts[2]
    else:
        return "unknown"


def _get_build_model(env):
    build_label_parts = env["BUILD_LABEL"].split("-")
    if len(build_label_parts) > 3:
        return build_label_parts[3]
    else:
        return "7seg"


def _get_version_string(env):
    official_part = (
        "c" if env["GIT_IS_COMMUNITY"] else "s" if env["GIT_IS_OFFICIAL"] else "f"
    )
    date_part = env["BUILD_DATE"].strftime("%Y%m%d")
    commit_part = env["GIT_COMMIT"][0:7]
    build_type_part = env["BUILD_TYPE"][0]
    dirty_part = "d" if env["GIT_DIRTY"] else "c"

    return "{}{}-{}-{}{}".format(
        official_part, date_part, commit_part, build_type_part, dirty_part
    )


def _create_header_from_template(target, source, env):
    env.Tool("textfile")

    env["TEMPLATEHEADERCOMSTR"] = "Generating header $TARGET from template $SOURCE."

    Action(
        env.Substfile(
            target,
            source,
            SUBST_DICT=env["TEMPLATE_SUBST_DICT"],
        ),
        env["TEMPLATEHEADERCOMSTR"],
    )


def exists():
    return True


def generate(env):
    source = os.path.join(env["SOURCE_DIR"], "DBT.h")
    target = os.path.join(env["BUILD_DIR"], "include", "DBT.h")

    env["BUILD_DATE"] = datetime.now()
    env["BUILD_TYPE"] = _get_build_type(env)
    env["BUILD_MODEL"] = _get_build_model(env)
    _prepare_git_info(env)

    # By your powers combined...
    env["VERSION_STRING"] = _get_version_string(env)
    env.Append(CPPDEFINES='DBT_FW_VERSION_STRING=\\"$VERSION_STRING\\"')
    # env["TEMPLATE_SUBST_DICT"] = {
    #     "(DBT_TMPL_VERSION_STRING)": '"{}"'.format(env["VERSION_STRING"]),
    #     "(DBT_TMPL_BUILD_RELEASE)": 1 if env["BUILD_TYPE"] == "release" else 0,
    #     "(DBT_TMPL_HAVE_OLED)": 1 if env["BUILD_MODEL"] == "oled" else 0,
    # }

    # _create_header_from_template(target, source, env)

    vprint("Build Label    : {}".format(env["BUILD_LABEL"]))
    vprint("Build Date     : {}".format(env["BUILD_DATE"].strftime("%Y-%m-%d")))
    vprint("Git Branch     : {}".format(env["GIT_BRANCH_NAME"]))
    vprint(
        "Git Commit     : {}{}".format(
            env["GIT_COMMIT"], "-dirty" if env["GIT_DIRTY"] else ""
        )
    )
    vprint("Version String : {}".format(env["VERSION_STRING"]))
    vprint()
