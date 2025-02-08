# clang-tidy review
# Copyright (c) 2020 Peter Hill
# SPDX-License-Identifier: MIT
# See LICENSE for more information

import argparse
import base64
import contextlib
import datetime
import fnmatch
import io
import itertools
import json
import multiprocessing
import os
import pathlib
import pprint
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import threading
import zipfile
from operator import itemgetter
from pathlib import Path
from typing import Any, Optional, TypedDict

import unidiff
import urllib3
import yaml
from github import Auth, Github
from github.PaginatedList import PaginatedList
from github.PullRequest import ReviewComment
from github.Requester import Requester
from github.WorkflowRun import WorkflowRun

DIFF_HEADER_LINE_LENGTH = 5
FIXES_FILE = Path("clang_tidy_review.yaml")
METADATA_FILE = Path("clang-tidy-review-metadata.json")
REVIEW_FILE = Path("clang-tidy-review-output.json")
PROFILE_DIR = Path("clang-tidy-review-profile")
MAX_ANNOTATIONS = 10


class Metadata(TypedDict):
    """Loaded from `METADATA_FILE`
    Contains information necessary to post a review without pull request knowledge

    """

    pr_number: int


class PRReview(TypedDict):
    body: str
    event: str
    comments: list[ReviewComment]


class HashableComment:
    def __init__(self, body: str, line: int, path: str, side: str, **kwargs):
        self.body = body
        self.line = line
        self.path = path
        self.side = side

    def __hash__(self):
        return hash(
            (
                self.body,
                self.line,
                self.path,
                self.side,
            )
        )

    def __eq__(self, other):
        return (
            type(self) is type(other)
            and self.body == other.body
            and self.line == self.line
            and other.path == other.path
            and self.side == other.side
        )

    def __lt__(self, other):
        if self.path != other.path:
            return self.path < other.path
        if self.line != other.line:
            return self.line < other.line
        if self.side != other.side:
            return self.side < other.side
        if self.body != other.body:
            return self.body < other.body
        return id(self) < id(other)


def add_auth_arguments(parser: argparse.ArgumentParser):
    # Token
    parser.add_argument("--token", help="github auth token")
    # App
    group_app = parser.add_argument_group(
        """Github app installation authentication
Permissions required: Contents (Read) and Pull requests (Read and Write)"""
    )
    group_app.add_argument("--app-id", type=int, help="app ID")
    group_app.add_argument(
        "--private-key", type=str, help="app private key as a string"
    )
    group_app.add_argument(
        "--private-key-base64",
        type=str,
        help="app private key as a string encoded as base64",
    )
    group_app.add_argument(
        "--private-key-file-path",
        type=pathlib.Path,
        help="app private key .pom file path",
    )
    group_app.add_argument("--installation-id", type=int, help="app installation ID")


def get_auth_from_arguments(args: argparse.Namespace) -> Auth.Auth:
    if args.token:
        return Auth.Token(args.token)

    if (
        args.app_id
        and (args.private_key or args.private_key_file_path or args.private_key_base64)
        and args.installation_id
    ):
        if args.private_key:
            private_key = args.private_key
        elif args.private_key_base64:
            private_key = base64.b64decode(args.private_key_base64).decode("ascii")
        else:
            private_key = pathlib.Path(args.private_key_file_path).read_text()
        return Auth.AppAuth(args.app_id, private_key).get_installation_auth(
            args.installation_id
        )
    if (
        args.app_id
        or args.private_key
        or args.private_key_file_path
        or args.private_key_base64
        or args.installation_id
    ):
        raise argparse.ArgumentError(
            None,
            "--app-id, --private-key[-file-path|-base64] and --installation-id must be supplied together",
        )

    raise argparse.ArgumentError(None, "authentication method not supplied")


def build_clang_tidy_warnings(
    base_invocation: list,
    env: dict,
    tmpdir: Path,
    task_queue: queue.Queue,
    lock: threading.Lock,
    failed_files: list,
) -> None:
    """Run clang-tidy on the given files and save output into a temporary file"""

    while True:
        name = task_queue.get()
        invocation = base_invocation[:]

        # Get a temporary file. We immediately close the handle so clang-tidy can
        # overwrite it.
        (handle, fixes_file) = tempfile.mkstemp(suffix=".yaml", dir=tmpdir)
        os.close(handle)
        invocation.append(f"--export-fixes={fixes_file}")

        invocation.append(name)

        proc = subprocess.Popen(
            invocation, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
        )
        output, err = proc.communicate()

        if proc.returncode != 0:
            if proc.returncode < 0:
                msg = f"{name}: terminated by signal {-proc.returncode}\n"
                err += msg.encode("utf-8")
            failed_files.append(name)
        with lock:
            subprocess.list2cmdline(invocation)
            sys.stdout.write(
                f"{name}: {subprocess.list2cmdline(invocation)}\n{output.decode('utf-8')}"
            )
            if len(err) > 0:
                sys.stdout.flush()
                sys.stderr.write(err.decode("utf-8"))

        task_queue.task_done()


def clang_tidy_version(clang_tidy_binary: pathlib.Path):
    try:
        version_out = subprocess.run(
            [clang_tidy_binary, "--version"],
            capture_output=True,
            check=True,
            text=True,
        ).stdout
    except subprocess.CalledProcessError as e:
        print(f"\n\nWARNING: Couldn't get clang-tidy version, error was: {e}")
        return 0

    if version := re.search(r"version (\d+)", version_out):
        return int(version.group(1))

    print(
        f"\n\nWARNING: Couldn't get clang-tidy version number, '{clang_tidy_binary} --version' reported: {version_out}"
    )
    return 0


def config_file_or_checks(
    clang_tidy_binary: pathlib.Path, clang_tidy_checks: str, config_file: str
) -> Optional[str]:
    version = clang_tidy_version(clang_tidy_binary)

    if config_file == "":
        if clang_tidy_checks:
            return f"--checks={clang_tidy_checks}"
        return None

    if version >= 12:
        return f"--config-file={config_file}"

    if config_file != ".clang-tidy":
        print(
            f"\n\nWARNING: non-default config file name '{config_file}' will be ignored for "
            "selected clang-tidy version {version}. This version expects exactly '.clang-tidy'\n"
        )

    return "--config"


def merge_replacement_files(tmpdir: Path, mergefile: Path):
    """Merge all replacement files in a directory into a single file"""
    # The fixes suggested by clang-tidy >= 4.0.0 are given under
    # the top level key 'Diagnostics' in the output yaml files
    mergekey = "Diagnostics"
    merged = []
    for replacefile in tmpdir.glob("*.yaml"):
        with replacefile.open() as f:
            content = yaml.safe_load(f)
        if not content:
            continue  # Skip empty files.
        merged.extend(content.get(mergekey, []))

    if merged:
        # MainSourceFile: The key is required by the definition inside
        # include/clang/Tooling/ReplacementsYaml.h, but the value
        # is actually never used inside clang-apply-replacements,
        # so we set it to '' here.
        output = {"MainSourceFile": "", mergekey: merged}
        with mergefile.open("w") as out:
            yaml.safe_dump(output, out)


def load_clang_tidy_warnings(fixes_file: Path) -> dict:
    """Read clang-tidy warnings from fixes_file. Can be produced by build_clang_tidy_warnings"""
    try:
        with fixes_file.open() as f:
            return yaml.safe_load(f)
    except FileNotFoundError:
        return {}


class PullRequest:
    """Add some convenience functions not in PyGithub"""

    def __init__(self, repo: str, pr_number: Optional[int], auth: Auth.Auth) -> None:
        self.repo_name = repo
        self.pr_number = pr_number
        self.auth = auth

        # Choose API URL, default to public GitHub
        self.api_url = os.environ.get("GITHUB_API_URL", "https://api.github.com")

        github = Github(auth=self.auth, base_url=self.api_url)
        self.repo = github.get_repo(f"{repo}")
        self._pull_request = None

    @property
    def token(self):
        return self.auth.token

    @property
    def pull_request(self):
        if self._pull_request is None:
            if self.pr_number is None:
                raise RuntimeError("Missing PR number")

            self._pull_request = self.repo.get_pull(int(self.pr_number))
        return self._pull_request

    @property
    def head_sha(self):
        if self._pull_request is None:
            raise RuntimeError("Missing PR")

        return self._pull_request.get_commits().reversed[0].sha

    def get_pr_diff(self) -> list[unidiff.PatchedFile]:
        """Download the PR diff, return a list of PatchedFile"""

        _, data = self.repo._requester.requestJsonAndCheck(
            "GET",
            self.pull_request.url,
            headers={"Accept": f"application/vnd.github.{'v3.diff'}"},
        )
        if not data:
            return []

        diffs = data["data"]

        # PatchSet is the easiest way to construct what we want, but the
        # diff_line_no property on lines is counted from the top of the
        # whole PatchSet, whereas GitHub is expecting the "position"
        # property to be line count within each file's diff. So we need to
        # do this little bit of faff to get a list of file-diffs with
        # their own diff_line_no range
        return [unidiff.PatchSet(str(file))[0] for file in unidiff.PatchSet(diffs)]

    def get_pr_author(self) -> str:
        """Get the username of the PR author. This is used in google-readability-todo"""
        return self.pull_request.user.login

    def get_pr_comments(self):
        """Download the PR review comments using the comfort-fade preview headers"""

        def get_element(
            requester: Requester, headers: dict, element: dict, completed: bool
        ):
            return element

        return PaginatedList(
            get_element,
            self.pull_request._requester,
            self.pull_request.review_comments_url,
            None,
        )

    def post_lgtm_comment(self, body: str):
        """Post a "LGTM" comment if everything's clean, making sure not to spam"""

        if not body:
            return

        comments = self.get_pr_comments()

        for comment in comments:
            if comment["body"] == body:
                print("Already posted, no need to update")
                return

        self.pull_request.create_issue_comment(body)

    def post_review(self, review: PRReview):
        """Submit a completed review"""
        self.pull_request.create_review(**review)

    def post_annotations(self, review):
        headers = {
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {self.token}",
        }
        url = f"{self.api_url}/repos/{self.repo_name}/check-runs"

        self.repo._requester.requestJsonAndCheck(
            "POST", url, parameters=review, headers=headers
        )


@contextlib.contextmanager
def message_group(title: str):
    print(f"::group::{title}", flush=True)
    try:
        yield
    finally:
        print("::endgroup::", flush=True)


def make_file_line_lookup(diff):
    """Get a lookup table for each file in diff, to convert between source
    line number to line number in the diff

    """
    lookup = {}
    for file in diff:
        filename = file.target_file[2:]
        lookup[filename] = {}
        for hunk in file:
            for line in hunk:
                if line.diff_line_no is None:
                    continue
                if not line.is_removed:
                    lookup[filename][line.target_line_no] = (
                        line.diff_line_no - DIFF_HEADER_LINE_LENGTH
                    )
    return lookup


def make_file_offset_lookup(filenames):
    """Create a lookup table to convert between character offset and line
    number for the list of files in `filenames`.

    This is a dict of the cumulative sum of the line lengths for each file.

    """
    lookup = {}

    for filename in filenames:
        with Path(filename).open() as file:
            lines = file.readlines()
        # Length of each line
        line_lengths = map(len, lines)
        # Cumulative sum of line lengths => offset at end of each line
        lookup[Path(filename).resolve().as_posix()] = [
            0,
            *list(itertools.accumulate(line_lengths)),
        ]

    return lookup


def get_diagnostic_file_path(clang_tidy_diagnostic, build_dir):
    # Sometimes, clang-tidy gives us an absolute path, so everything is fine.
    # Sometimes however it gives us a relative path that is realtive to the
    # build directory, so we prepend that.

    # Modern clang-tidy
    if ("DiagnosticMessage" in clang_tidy_diagnostic) and (
        "FilePath" in clang_tidy_diagnostic["DiagnosticMessage"]
    ):
        file_path = clang_tidy_diagnostic["DiagnosticMessage"]["FilePath"]
        if file_path == "":
            return ""
        file_path = Path(file_path)
        if file_path.is_absolute():
            return os.path.normpath(file_path.resolve())
        if "BuildDirectory" in clang_tidy_diagnostic:
            return os.path.normpath(
                (Path(clang_tidy_diagnostic["BuildDirectory"]) / file_path).resolve()
            )
        return os.path.normpath(file_path.resolve())

    # Pre-clang-tidy-9 format
    if "FilePath" in clang_tidy_diagnostic:
        file_path = clang_tidy_diagnostic["FilePath"]
        if file_path == "":
            return ""
        return os.path.normpath((Path(build_dir) / file_path).resolve())

    return ""


def find_line_number_from_offset(offset_lookup, filename, offset):
    """Work out which line number `offset` corresponds to using `offset_lookup`.

    The line number (0-indexed) is the index of the first line offset
    which is larger than `offset`.

    """
    name = str(pathlib.Path(filename).resolve().absolute())

    if name not in offset_lookup:
        # Let's make sure we've the file offsets for this other file
        offset_lookup.update(make_file_offset_lookup([name]))

    for line_num, line_offset in enumerate(offset_lookup[name]):
        if line_offset > offset:
            return line_num - 1
    return -1


def read_one_line(filename, line_offset):
    """Read a single line from a source file"""
    # Could cache the files instead of opening them each time?
    with Path(filename).open() as file:
        file.seek(line_offset)
        return file.readline().rstrip("\n")


def collate_replacement_sets(diagnostic, offset_lookup):
    """Return a dict of replacements on the same or consecutive lines, indexed by line number

    We need this as we have to apply all the replacements on one line at the same time

    This could break if there are replacements in with the same line
    number but in different files.

    """

    # First, make sure each replacement contains "LineNumber", and
    # "EndLineNumber" in case it spans multiple lines
    for replacement in diagnostic["Replacements"]:
        # Sometimes, the FilePath may include ".." in "." as a path component
        # However, file paths are stored in the offset table only after being
        # converted to an abs path, in which case the stored path will differ
        # from the FilePath and we'll end up looking for a path that's not in
        # the lookup dict
        # To fix this, we'll convert all the FilePaths to absolute paths
        replacement["FilePath"] = Path(replacement["FilePath"]).resolve().as_posix()

        # It's possible the replacement is needed in another file?
        # Not really sure how that could come about, but let's
        # cover our behinds in case it does happen:
        if replacement["FilePath"] not in offset_lookup:
            # Let's make sure we've the file offsets for this other file
            offset_lookup.update(make_file_offset_lookup([replacement["FilePath"]]))

        replacement["LineNumber"] = find_line_number_from_offset(
            offset_lookup, replacement["FilePath"], replacement["Offset"]
        )
        replacement["EndLineNumber"] = find_line_number_from_offset(
            offset_lookup,
            replacement["FilePath"],
            replacement["Offset"] + replacement["Length"],
        )

    # Now we can group them into consecutive lines
    groups = []
    for index, replacement in enumerate(diagnostic["Replacements"]):
        if index == 0:
            # First one starts a new group, always
            groups.append([replacement])
        elif (
            replacement["LineNumber"] == groups[-1][-1]["LineNumber"]
            or replacement["LineNumber"] - 1 == groups[-1][-1]["LineNumber"]
        ):
            # Same or adjacent line to the last line in the last group
            # goes in the same group
            groups[-1].append(replacement)
        else:
            # Otherwise, start a new group
            groups.append([replacement])

    # Turn the list into a dict
    return {g[0]["LineNumber"]: g for g in groups}


def replace_one_line(replacement_set, line_num, offset_lookup):
    """Apply all the replacements in replacement_set at the same time"""

    filename = replacement_set[0]["FilePath"]
    # File offset at the start of the first line
    line_offset = offset_lookup[filename][line_num]

    # list of (start, end) offsets from line_offset
    insert_offsets: list[tuple[Optional[int], Optional[int]]] = [(0, 0)]
    # Read all the source lines into a dict so we only get one copy of
    # each line, though we might read the same line in multiple times
    source_lines = {}
    for replacement in replacement_set:
        start = replacement["Offset"] - line_offset
        end = start + replacement["Length"]
        insert_offsets.append((start, end))

        # Make sure to read any extra lines we need too
        for replacement_line_num in range(
            replacement["LineNumber"], replacement["EndLineNumber"] + 1
        ):
            replacement_line_offset = offset_lookup[filename][replacement_line_num]
            source_lines[replacement_line_num] = (
                read_one_line(filename, replacement_line_offset) + "\n"
            )

    # Replacements might cross multiple lines, so squash them all together
    source_line = "".join(source_lines.values()).rstrip("\n")

    insert_offsets.append((None, None))

    fragments = []
    for (_, start), (end, _) in zip(insert_offsets[:-1], insert_offsets[1:]):
        fragments.append(source_line[start:end])

    new_line = ""
    for fragment, replacement in zip(fragments, replacement_set):
        new_line += fragment + replacement["ReplacementText"]

    return source_line, new_line + fragments[-1]


def format_ordinary_line(source_line, line_offset):
    """Format a single C++ line with a diagnostic indicator"""

    return textwrap.dedent(
        f"""\
         ```cpp
         {source_line}
         {line_offset * " " + "^"}
         ```
         """
    )


def format_diff_line(diagnostic, offset_lookup, source_line, line_offset, line_num):
    """Format a replacement as a Github suggestion or diff block"""

    end_line = line_num

    # We're going to be appending to this
    code_blocks = ""

    replacement_sets = collate_replacement_sets(diagnostic, offset_lookup)

    for replacement_line_num, replacement_set in replacement_sets.items():
        old_line, new_line = replace_one_line(
            replacement_set, replacement_line_num, offset_lookup
        )

        print(f"----------\n{old_line=}\n{new_line=}\n----------")

        # If the replacement is for the same line as the
        # diagnostic (which is where the comment will be), then
        # format the replacement as a suggestion. Otherwise,
        # format it as a diff
        if replacement_line_num == line_num:
            code_blocks += f"""
```suggestion
{new_line}
```
"""
            end_line = replacement_set[-1]["EndLineNumber"]
        else:
            # Prepend each line in the replacement line with "+ "
            # in order to make a nice diff block. The extra
            # whitespace is so the multiline dedent-ed block below
            # doesn't come out weird.
            whitespace = "\n                "
            new_line = whitespace.join([f"+ {line}" for line in new_line.splitlines()])
            old_line = whitespace.join([f"- {line}" for line in old_line.splitlines()])

            rel_path = try_relative(replacement_set[0]["FilePath"]).as_posix()
            code_blocks += textwrap.dedent(
                f"""\

                {rel_path}:{replacement_line_num}:
                ```diff
                {old_line}
                {new_line}
                ```
                """
            )
    return code_blocks, end_line


def try_relative(path) -> pathlib.Path:
    """Try making `path` relative to current directory, otherwise make it an absolute path"""
    try:
        here = pathlib.Path.cwd()
        return pathlib.Path(path).relative_to(here)
    except ValueError:
        return pathlib.Path(path).resolve()


def fix_absolute_paths(build_compile_commands, base_dir):
    """Update absolute paths in compile_commands.json to new location, if
    compile_commands.json was created outside the Actions container
    """

    basedir = pathlib.Path(base_dir).resolve()
    newbasedir = Path.cwd()

    if basedir == newbasedir:
        return

    print(f"Found '{build_compile_commands}', updating absolute paths")
    # We might need to change some absolute paths if we're inside
    # a docker container
    with Path(build_compile_commands).open() as f:
        compile_commands = json.load(f)

    print(f"Replacing '{basedir}' with '{newbasedir}'", flush=True)

    modified_compile_commands = json.dumps(compile_commands).replace(
        str(basedir), str(newbasedir)
    )

    with Path(build_compile_commands).open("w") as f:
        f.write(modified_compile_commands)


def format_notes(notes, offset_lookup):
    """Format an array of notes into a single string"""

    code_blocks = ""

    for note in notes:
        filename = note["FilePath"]

        if filename == "":
            return note["Message"]

        resolved_path = str(pathlib.Path(filename).resolve().absolute())

        line_num = find_line_number_from_offset(
            offset_lookup, resolved_path, note["FileOffset"]
        )
        line_offset = note["FileOffset"] - offset_lookup[resolved_path][line_num]
        source_line = read_one_line(
            resolved_path, offset_lookup[resolved_path][line_num]
        )

        path = try_relative(resolved_path)
        message = f"**{path.as_posix()}:{line_num}:** {note['Message']}"
        code = format_ordinary_line(source_line, line_offset)
        code_blocks += f"{message}\n{code}"

    if notes:
        code_blocks = f"<details>\n<summary>Additional context</summary>\n\n{code_blocks}\n</details>\n"

    return code_blocks


def make_comment_from_diagnostic(
    diagnostic_name, diagnostic, filename, offset_lookup, notes
):
    """Create a comment from a diagnostic

    Comment contains the diagnostic message, plus its name, along with
    code block(s) containing either the exact location of the
    diagnostic, or suggested fix(es).

    """

    line_num = find_line_number_from_offset(
        offset_lookup, filename, diagnostic["FileOffset"]
    )
    line_offset = diagnostic["FileOffset"] - offset_lookup[filename][line_num]

    source_line = read_one_line(filename, offset_lookup[filename][line_num])
    end_line = line_num

    print(
        f"""{diagnostic}
    {line_num=};    {line_offset=};    {source_line=}
    """
    )

    if diagnostic["Replacements"]:
        code_blocks, end_line = format_diff_line(
            diagnostic, offset_lookup, source_line, line_offset, line_num
        )
    else:
        # No fixit, so just point at the problem
        code_blocks = format_ordinary_line(source_line, line_offset)

    code_blocks += format_notes(notes, offset_lookup)

    comment_body = (
        f"warning: {diagnostic['Message']} [{diagnostic_name}]\n{code_blocks}"
    )

    return comment_body, end_line + 1


def create_review_file(
    clang_tidy_warnings, diff_lookup, offset_lookup, build_dir
) -> Optional[PRReview]:
    """Create a Github review from a set of clang-tidy diagnostics"""

    if "Diagnostics" not in clang_tidy_warnings:
        return None

    comments: list[ReviewComment] = []

    for diagnostic in clang_tidy_warnings["Diagnostics"]:
        try:
            diagnostic_message = diagnostic["DiagnosticMessage"]
        except KeyError:
            # Pre-clang-tidy-9 format
            diagnostic_message = diagnostic

        if diagnostic_message["FilePath"] == "":
            continue

        comment_body, end_line = make_comment_from_diagnostic(
            diagnostic["DiagnosticName"],
            diagnostic_message,
            get_diagnostic_file_path(diagnostic, build_dir),
            offset_lookup,
            notes=diagnostic.get("Notes", []),
        )

        rel_path = try_relative(
            get_diagnostic_file_path(diagnostic, build_dir)
        ).as_posix()
        # diff lines are 1-indexed
        source_line = 1 + find_line_number_from_offset(
            offset_lookup,
            get_diagnostic_file_path(diagnostic, build_dir),
            diagnostic_message["FileOffset"],
        )

        if rel_path not in diff_lookup or end_line not in diff_lookup[rel_path]:
            print(
                f"WARNING: Skipping comment for file '{rel_path}' not in PR changeset. Comment body is:\n{comment_body}"
            )
            continue

        comments.append(
            {
                "path": rel_path,
                "body": comment_body,
                "side": "RIGHT",
                "line": end_line,
            }
        )
        # If this is a multiline comment, we need a couple more bits:
        if end_line != source_line:
            comments[-1].update(
                {
                    "start_side": "RIGHT",
                    "start_line": source_line,
                }
            )

    review: PRReview = {
        "body": "clang-tidy has made some suggestions. Please note that they are machine-generated, and you should review them carefully before applying to avoid introducing bugs.",
        "event": "COMMENT",
        "comments": comments,
    }
    return review


def make_timing_summary(
    clang_tidy_profiling: dict, real_time: datetime.timedelta, sha: Optional[str] = None
) -> str:
    if not clang_tidy_profiling:
        return ""
    top_amount = 10
    wall_key = "time.clang-tidy.total.wall"
    user_key = "time.clang-tidy.total.user"
    sys_key = "time.clang-tidy.total.sys"
    total_wall = sum(timings[wall_key] for timings in clang_tidy_profiling.values())
    total_user = sum(timings[user_key] for timings in clang_tidy_profiling.values())
    total_sys = sum(timings[sys_key] for timings in clang_tidy_profiling.values())
    print(f"Took: {total_user:.2f}s user {total_sys:.2f} system {total_wall:.2f} total")
    file_summary = textwrap.dedent(
        f"""\
        ### Top {top_amount} files
        | File  | user (s)         | system (s)      | total (s)        |
        | ----- | ---------------- | --------------- | ---------------- |
        | Total | {total_user:.2f} | {total_sys:.2f} | {total_wall:.2f} |
        """
    )
    topfiles = sorted(
        (
            (
                os.path.relpath(file),
                timings[user_key],
                timings[sys_key],
                timings[wall_key],
            )
            for file, timings in clang_tidy_profiling.items()
        ),
        key=lambda x: x[3],
        reverse=True,
    )

    if "GITHUB_SERVER_URL" in os.environ and "GITHUB_REPOSITORY" in os.environ:
        blob = f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/blob/{sha}"
    else:
        blob = None
    for f, u, s, w in list(topfiles)[:top_amount]:
        if blob is not None:
            f = f"[{f}]({blob}/{f})"
        file_summary += f"|{f}|{u:.2f}|{s:.2f}|{w:.2f}|\n"

    check_timings = {}
    for timings in clang_tidy_profiling.values():
        for check, timing in timings.items():
            if check in [wall_key, user_key, sys_key]:
                continue
            base_check, time_type = check.rsplit(".", 1)
            check_name = base_check.split(".", 2)[2]
            t = check_timings.get(check_name, (0.0, 0.0, 0.0))
            if time_type == "user":
                t = t[0] + timing, t[1], t[2]
            elif time_type == "sys":
                t = t[0], t[1] + timing, t[2]
            elif time_type == "wall":
                t = t[0], t[1], t[2] + timing
            check_timings[check_name] = t

    check_summary = ""
    if check_timings:
        check_summary = textwrap.dedent(
            f"""\
            ### Top {top_amount} checks
            | Check | user (s) | system (s) | total (s) |
            | ----- | -------- | ---------- | --------- |
            | Total | {total_user:.2f} | {total_sys:.2f} | {total_wall:.2f} |
            """
        )
        topchecks = sorted(
            ((check_name, *timings) for check_name, timings in check_timings.items()),
            key=lambda x: x[3],
            reverse=True,
        )
        for c, u, s, w in list(topchecks)[:top_amount]:
            c = decorate_check_names(f"[{c}]").replace("[[", "[").rstrip("]")
            check_summary += f"|{c}|{u:.2f}|{s:.2f}|{w:.2f}|\n"

    return (
        f"## Timing\nReal time: {real_time.seconds:.2f}\n{file_summary}{check_summary}"
    )


def filter_files(diff, include: list[str], exclude: list[str]) -> list:
    changed_files = [filename.target_file[2:] for filename in diff]
    files = []
    for pattern in include:
        files.extend(fnmatch.filter(changed_files, pattern))
        print(f"include: {pattern}, file list now: {files}")
    for pattern in exclude:
        files = [f for f in files if not fnmatch.fnmatch(f, pattern)]
        print(f"exclude: {pattern}, file list now: {files}")

    return files


def create_review(
    pull_request: PullRequest,
    build_dir: str,
    clang_tidy_checks: str,
    clang_tidy_binary: pathlib.Path,
    config_file: str,
    max_task: int,
    extra_arg_befores: list[str],
    include: list[str],
    exclude: list[str],
) -> Optional[PRReview]:
    """Given the parameters, runs clang-tidy and creates a review.
    If no files were changed, or no warnings could be found, None will be returned.

    """

    if max_task == 0:
        max_task = multiprocessing.cpu_count()

    diff = pull_request.get_pr_diff()
    print(f"\nDiff from GitHub PR:\n{diff}\n")

    files = filter_files(diff, include, exclude)

    if files == []:
        with message_group("No files to check!"), REVIEW_FILE.open("w") as review_file:
            json.dump(
                {
                    "body": "clang-tidy found no files to check",
                    "event": "COMMENT",
                    "comments": [],
                },
                review_file,
            )
        return None

    print(f"Checking these files: {files}", flush=True)

    line_ranges = get_line_ranges(diff, files)
    if line_ranges == "[]":
        with (
            message_group("No lines added in this PR!"),
            REVIEW_FILE.open("w") as review_file,
        ):
            json.dump(
                {
                    "body": "clang-tidy found no lines added",
                    "event": "COMMENT",
                    "comments": [],
                },
                review_file,
            )
        return None

    print(f"Line filter for clang-tidy:\n{line_ranges}\n")

    username = pull_request.get_pr_author() or "your name here"

    # Run clang-tidy with the configured parameters and produce the CLANG_TIDY_FIXES file
    export_fixes_dir = Path(tempfile.mkdtemp())
    env = dict(os.environ, USER=username)
    config = config_file_or_checks(clang_tidy_binary, clang_tidy_checks, config_file)
    base_invocation = [
        clang_tidy_binary,
        f"-p={build_dir}",
        f"-line-filter={line_ranges}",
        "--enable-check-profile",
        f"-store-check-profile={PROFILE_DIR}",
    ]
    if config:
        print(f"Using config: {config}")
        base_invocation.append(config)
    else:
        print("Using recursive directory config")

    base_invocation += [f"--extra-arg-before={arg}" for arg in extra_arg_befores]

    print(f"Spawning a task queue with {max_task} processes")
    start = datetime.datetime.now()
    try:
        # Spin up a bunch of tidy-launching threads.
        task_queue = queue.Queue(max_task)
        # list of files with a non-zero return code.
        failed_files = []
        lock = threading.Lock()
        for _ in range(max_task):
            t = threading.Thread(
                target=build_clang_tidy_warnings,
                args=(
                    base_invocation,
                    env,
                    export_fixes_dir,
                    task_queue,
                    lock,
                    failed_files,
                ),
            )
            t.daemon = True
            t.start()

        # Fill the queue with files.
        for name in files:
            task_queue.put(name)

        # Wait for all threads to be done.
        task_queue.join()

    except KeyboardInterrupt:
        # This is a sad hack. Unfortunately subprocess goes
        # bonkers with ctrl-c and we start forking merrily.
        print("\nCtrl-C detected, goodbye.")
        os.kill(0, 9)
        raise
    real_duration = datetime.datetime.now() - start

    # Read and parse the CLANG_TIDY_FIXES file
    print(f"Writing fixes to {FIXES_FILE} ...")
    merge_replacement_files(export_fixes_dir, FIXES_FILE)
    shutil.rmtree(export_fixes_dir)
    clang_tidy_warnings = load_clang_tidy_warnings(FIXES_FILE)

    # Read and parse the timing data
    clang_tidy_profiling = load_and_merge_profiling()

    try:
        sha = pull_request.head_sha
    except Exception:
        sha = os.environ.get("GITHUB_SHA")

    # Post to the action job summary
    step_summary = make_timing_summary(clang_tidy_profiling, real_duration, sha)
    set_summary(step_summary)

    print("clang-tidy had the following warnings:\n", clang_tidy_warnings, flush=True)

    diff_lookup = make_file_line_lookup(diff)
    offset_lookup = make_file_offset_lookup(files)

    with message_group("Creating review from warnings"):
        review = create_review_file(
            clang_tidy_warnings, diff_lookup, offset_lookup, build_dir
        )
        with REVIEW_FILE.open("w") as review_file:
            json.dump(review, review_file)

        return review


def download_artifacts(pull: PullRequest, workflow_id: int):
    """Attempt to automatically download the artifacts from a previous
    run of the review Action"""

    # workflow id is an input: ${{github.event.workflow_run.id }}
    workflow: WorkflowRun = pull.repo.get_workflow_run(workflow_id)
    # I don't understand why mypy complains about the next line!
    for artifact in workflow.get_artifacts():
        if artifact.name == "clang-tidy-review":
            break
    else:
        # Didn't find the artefact, so bail
        print(
            f"Couldn't find 'clang-tidy-review' artifact for workflow '{workflow_id}'. "
            f"Available artifacts are: {list(workflow.get_artifacts())}"
        )
        return None, None

    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {pull.token}",
    }
    r = urllib3.request("GET", artifact.archive_download_url, headers=headers)
    if r.status != 200:
        print(
            f"WARNING: Couldn't automatically download artifacts for workflow '{workflow_id}', response was: {r}: {r.reason}"
        )
        return None, None

    data = zipfile.ZipFile(io.BytesIO(r.data))
    filenames = data.namelist()

    metadata = (
        json.loads(data.read(str(METADATA_FILE)))
        if str(METADATA_FILE) in filenames
        else None
    )
    review = (
        json.loads(data.read(str(REVIEW_FILE)))
        if str(REVIEW_FILE) in filenames
        else None
    )
    return metadata, review


def load_metadata() -> Optional[Metadata]:
    """Load metadata from the METADATA_FILE path"""

    if not METADATA_FILE.exists():
        print(f"WARNING: Could not find metadata file ('{METADATA_FILE}')", flush=True)
        return None

    with METADATA_FILE.open() as metadata_file:
        return json.load(metadata_file)


def save_metadata(pr_number: int) -> None:
    """Save metadata to the METADATA_FILE path"""

    metadata: Metadata = {"pr_number": pr_number}

    with METADATA_FILE.open("w") as metadata_file:
        json.dump(metadata, metadata_file)


def load_review(review_file: pathlib.Path) -> Optional[PRReview]:
    """Load review output"""

    if not review_file.exists():
        print(f"WARNING: Could not find review file ('{review_file}')", flush=True)
        return None

    with review_file.open() as review_file_handle:
        payload = json.load(review_file_handle)
        return payload or None


def load_and_merge_profiling() -> dict:
    result = {}
    for profile_file in PROFILE_DIR.glob("*.json"):
        profile_dict = json.load(profile_file.open())
        filename = profile_dict["file"]
        current_profile = result.get(filename, {})
        for check, timing in profile_dict["profile"].items():
            current_profile[check] = current_profile.get(check, 0.0) + timing
        result[filename] = current_profile
    for filename, timings in list(result.items()):
        timings["time.clang-tidy.total.wall"] = sum(
            v for k, v in timings.items() if k.endswith("wall")
        )
        timings["time.clang-tidy.total.user"] = sum(
            v for k, v in timings.items() if k.endswith("user")
        )
        timings["time.clang-tidy.total.sys"] = sum(
            v for k, v in timings.items() if k.endswith("sys")
        )
        result[filename] = timings
    return result


def load_and_merge_reviews(review_files: list[pathlib.Path]) -> Optional[PRReview]:
    reviews = []
    for file in review_files:
        review = load_review(file)
        if review is not None and len(review.get("comments", [])) > 0:
            reviews.append(review)

    if not reviews:
        return None

    result = reviews[0]

    comments = set()
    for review in reviews:
        comments.update(HashableComment(**c) for c in review["comments"])

    result["comments"] = [c.__dict__ for c in sorted(comments)]

    return result


def get_line_ranges(diff, files):
    """Return the line ranges of added lines in diff, suitable for the
    line-filter argument of clang-tidy

    """

    lines_by_file = {}
    for filename in diff:
        if filename.target_file[2:] not in files:
            continue
        added_lines = []
        for hunk in filename:
            for line in hunk:
                if line.is_added:
                    added_lines.append(line.target_line_no)

        for _, group in itertools.groupby(
            enumerate(added_lines), lambda ix: ix[0] - ix[1]
        ):
            groups = list(map(itemgetter(1), group))
            lines_by_file.setdefault(filename.target_file[2:], []).append(
                [groups[0], groups[-1]]
            )

    line_filter_json = []
    for name, lines in lines_by_file.items():
        line_filter_json.append({"name": name, "lines": lines})
        # On windows, unidiff has forward slashes but cl.exe expects backslashes.
        # However, clang.exe on windows expects forward slashes.
        # Adding a copy of the line filters with backslashes allows for both cl.exe and clang.exe to work.
        if os.path.sep == "\\":
            # Converts name to backslashes for the cl.exe line filter.
            name = Path.joinpath(*name.split("/"))
            line_filter_json.append({"name": name, "lines": lines})
    return json.dumps(line_filter_json, separators=(",", ":"))


def cull_comments(pull_request: PullRequest, review, max_comments):
    """Remove comments from review that have already been posted, and keep
    only the first max_comments

    """

    unposted_comments = {HashableComment(**c) for c in review["comments"]}
    posted_comments = {HashableComment(**c) for c in pull_request.get_pr_comments()}

    review["comments"] = [
        c.__dict__ for c in sorted(unposted_comments - posted_comments)
    ]

    if len(review["comments"]) > max_comments:
        review["body"] += (
            "\n\nThere were too many comments to post at once. "
            f"Showing the first {max_comments} out of {len(review['comments'])}. "
            "Check the log or trigger a new build to see more."
        )
        review["comments"] = review["comments"][:max_comments]

    return review


def strip_enclosing_quotes(string: str) -> str:
    """Strip leading/trailing whitespace and remove any enclosing quotes"""
    stripped = string.strip()

    # Need to check double quotes again in case they're nested inside
    # single quotes
    for quote in ['"', "'", '"']:
        if stripped.startswith(quote) and stripped.endswith(quote):
            stripped = stripped[1:-1]
    return stripped


def set_output(key: str, val: str) -> bool:
    if "GITHUB_OUTPUT" not in os.environ:
        return False

    # append key-val pair to file
    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as f:
        f.write(f"{key}={val}\n")

    return True


def set_summary(val: str) -> bool:
    if "GITHUB_STEP_SUMMARY" not in os.environ:
        return False

    # append key-val pair to file
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as f:
        f.write(val)

    return True


def decorate_check_names(comment: str) -> str:
    """
    Split on first dash into two groups of string in [] at end of line
    exception: if the first group starts with 'clang' such as 'clang-diagnostic-error'
    exception to the exception: if the string starts with 'clang-analyzer', in which case, make it the first group
    """
    version = "extra"
    url = f"https://clang.llvm.org/{version}/clang-tidy/checks"
    regex = r"(\[((?:clang-analyzer)|(?:(?!clang)[\w]+))-([\.\w-]+)\]$)"
    subst = f"[\\g<1>({url}/\\g<2>/\\g<3>.html)]"
    return re.sub(regex, subst, comment, count=1, flags=re.MULTILINE)


def decorate_comment(comment: ReviewComment) -> ReviewComment:
    comment["body"] = decorate_check_names(comment["body"])
    return comment


def decorate_comments(review: PRReview) -> PRReview:
    review["comments"] = list(map(decorate_comment, review["comments"]))
    return review


def post_review(
    pull_request: PullRequest,
    review: Optional[PRReview],
    max_comments: int,
    lgtm_comment_body: str,
    dry_run: bool,
) -> int:
    print(
        "Created the following review:\n", pprint.pformat(review, width=130), flush=True
    )

    if not review or review["comments"] == []:
        print("No warnings to report, LGTM!")
        if not dry_run:
            pull_request.post_lgtm_comment(lgtm_comment_body)
        return 0

    total_comments = len(review["comments"])

    set_output("total_comments", str(total_comments))

    decorated_review = decorate_comments(review)

    print("Removing already posted or extra comments", flush=True)
    trimmed_review = cull_comments(pull_request, decorated_review, max_comments)

    if not trimmed_review["comments"]:
        print("Everything already posted!")
        return total_comments

    if dry_run:
        pprint.pprint(review, width=130)
        return total_comments

    print("Posting the review:\n", pprint.pformat(trimmed_review), flush=True)
    pull_request.post_review(trimmed_review)

    return total_comments


def convert_comment_to_annotations(comment):
    return {
        "path": comment["path"],
        "start_line": comment.get("start_line", comment["line"]),
        "end_line": comment["line"],
        "annotation_level": "warning",
        "title": "clang-tidy",
        "message": comment["body"],
    }


def post_annotations(
    pull_request: PullRequest, review: Optional[PRReview]
) -> Optional[int]:
    """Post the first 10 comments in the review as annotations"""

    body: dict[str, Any] = {
        "name": "clang-tidy-review",
        "head_sha": pull_request.pull_request.head.sha,
        "status": "completed",
        "conclusion": "success",
    }

    if review is None:
        return None

    if review["comments"] == []:
        print("No warnings to report, LGTM!")
        pull_request.post_annotations(body)

    comments = []
    for comment in review["comments"]:
        first_line = comment["body"].splitlines()[0]
        comments.append(
            f"{comment['path']}:{comment.get('start_line', comment.get('line', 0))}: {first_line}"
        )

    total_comments = len(review["comments"])

    body["conclusion"] = "neutral"
    body["output"] = {
        "title": "clang-tidy-review",
        "summary": f"There were {total_comments} warnings",
        "text": "\n".join(comments),
        "annotations": [
            convert_comment_to_annotations(comment)
            for comment in review["comments"][:MAX_ANNOTATIONS]
        ],
    }

    pull_request.post_annotations(body)
    return total_comments


def bool_argument(user_input) -> bool:
    """Convert text to bool"""
    user_input = str(user_input).upper()
    if user_input == "TRUE":
        return True
    if user_input == "FALSE":
        return False
    raise ValueError("Invalid value passed to bool_argument")
