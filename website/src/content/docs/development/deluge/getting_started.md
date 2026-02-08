---
title: Getting Started
---

## Obtaining the Source Code

[Git](https://git-scm.com/) is the official tool used to manage the community repository. All the following instructions will assume you have it installed in your operating system and on the path, as it's necessary to both obtain the source code and to submit changes back to GitHub. The build tools also require Git as part of their versioning logic so working from a zip of the source code will not be sufficient.

If you only plan on building the codebase, you can open up your terminal/shell of choice, change to the directory on your system where you'd like the repository to live, and type:

```bash
git clone https://github.com/SynthstromAudible/DelugeFirmware.git
```

If you foresee yourself being an active contributor, consider making a fork of the repo by clicking the "Fork" button at the top of the project's [GitHub page](https://github.com/SynthstromAudible/DelugeFirmware), and then click the "<> Code" button on your local fork for details on what URL to clone.

### Storage Requirements

These vary slightly by operating system and architecture but the following should give you an idea of how much space you'll need:

- Git Repository: **~83 MB** (as of July 3, 2023)
- Toolchain + Archive: **~1.3 GB** (DBT-Toolchain v10)
- Built objects and images: **~150 MB** (as of July 3, 2023)

So a little over **1.5GB total**. Expect that this size will only grow over time.

## Building the Codebase in DBT

The official way to manage your toolchain and build the Deluge Firmware is using the included **Deluge Build Tool** (**DBT**). DBT is a comprehensive commandline suite written primarily in Python and CMake. DBT has no unusual outside dependencies and runs using standalone tools it fetches on its own through shell scripts. It should behave nicely on a freshly installed OS.

DBT runs and builds on:

- **Windows** - x86_64 only
  - _No need for WSL, cygwin, or mingw to be installed, and no weird things on the path. It's just cmd and powershell scripts to get started._
- **MacOS (Darwin)** - 64 bit Intel x86_64 and arm64 (M1 & M2 native)
  - _`bash` will need to be on the path, even if it's not your shell of choice._
- **Linux** - Intel x86_64 and arm64
  - _Linux depends on the presence of libxcrypt libraries, as directed in the kernel documentation (on Arch in particular, you may need to install the `libxcrypt-compat` package). `bash` will need to be on the path._

To run DBT, simply open up a command prompt or your terminal/shell in your operating system of choice, change directory to the git repository you cloned earlier, and type the following variation of the command, depending on your operating system:

- Windows (cmd): `dbt`, `dbt.cmd`, or `.\dbt.cmd`
- Windows (PowerShell): `./dbt`
- MacOS (Darwin): `./dbt`
- Linux: `./dbt`

The first time you run DBT, it will check for the toolchain in the `toolchain` folder and if it's missing it will start the process of downloading and installing. Nothing will be installed or extracted outside of the repository directory so there is no worry of your system becoming polluted with dependencies. The toolchain includes:

- xPack GNU ARM Embedded GCC (v13.2.1)
- xPack OpenOCD (v0.12.0)
- xPack Clang (v17.0.6) (only `clang-format` and `clang-tidy`)
- xPack CMake (v3.26.5)
- xPack Ninja (v1.11.1)
- Standalone Python (v3.12.1)
  - SSL/TLS Certificates (certifi)
  - Multiple additional Python libraries from a frequently changing list

Ignore any warnings about certificates Python may express prior to installation of its certificates package.

Once installed, DBT should let you know you can request help with `--help` and then you're good to go.

DBT must always be run from the _root directory_ of the repository. The remainder of the instructions will use the MacOS and Linux approach to calling DBT but substitute your own as needed.

### DBT Build Targets

These live under the `dbt build` subcommand. The general format is `dbt build {configuration}`, with configuration being optional (building both debug and release by default).

Built files output to subdirectories named after the configuration (e.g. `build/Release`).

#### Build configurations:

- `debug` - build selected target with debugging support (including debugging symbols and different optimizations)
- `release` - build selected target with optimizations for release.

#### Build arguments:

- `-S` or `--no-status` - this disables the `ninja` status line while building. Output from compilation commands (such as warnings and errors) will still be printed.
- `-v` or `--verbose` - this prints greater level of detail to the console (i.e. exactly the compiler/linker commands called)
- `-c` or `--clean-first` - clean before building

#### CMake custom arguments

Any additional arguments to CMake may be transparently passed via `dbt build`. Primarily this means `-j <jobs>` for limiting or defining the amount of parallelization. Most other relevant options are exposed via `dbt build`.
