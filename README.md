# About
The Synthstrom Audible Deluge Firmware runs on the Deluge portable sequencer, synthesizer and sampler, produced by Synthstrom Audible Limited.

The Deluge Firmware’s codebase is written mostly in C++, with some low-level functions in C, and even the occasional line of assembly. The program is “bare-metal” - it runs without any higher level operating system.

The firmware runs on the Synthstrom Audible Deluge’s Renesas RZ/A1L processor, an Arm® Cortex®-A9 core running at 400MHz with 3MB of on-chip SRAM. The Deluge also includes a 64MB SDRAM chip.

The firmware is built using a GNU ARM Embedded GCC toolchain.

### Kudos

Kudos to [Flipper Devices](https://github.com/flipperdevices/) for the GPL v3 build tool FBT from which Deluge Build Tool (DBT) heavily borrows both code and inspiration.

## Obtaining the Source Code

[Git](https://git-scm.com/) is the official tool used to manage the community repository. All the following instructions will assume you have it installed in your operating system and on the path, as it's necessary to both obtain the source code and to submit changes back to GitHub. The build tools also require Git as part of their versioning logic so working from a zip of the source code will not be sufficient.

If you only plan on building the codebase, you can open up your terminal/shell of choice, change to the directory on your system where you'd like the repository to live, and type:

```bash
git clone https://github.com/SynthstromAudible/DelugeFirmware.git
```

If you foresee yourself being an active contributor, consider making a fork of the repo by clicking the "Fork" button at the top of the project's [GitHub page](https://github.com/SynthstromAudible/DelugeFirmware), and then click the "<> Code" button on your local fork for details on what URL to clone.

### Storage Requirements

These vary slightly by operating system and architecture but the following should give you an idea of how much space you'll need:

* Git Repository: **~83 MB** (as of July 3, 2023)
* Toolchain + Archive: **~1.3 GB** (DBT-Toolchain v10)
* Built objects and images: **~150 MB** (as of July 3, 2023)

So a little over **1.5GB total**. Expect that this size will only grow over time.

## Building the Codebase in DBT

The official way to manage your toolchain and build the Deluge Firmware is using the included **Deluge Build Tool** (**DBT**). DBT is a comprehensive commandline suite written primarily in Python and CMake. DBT has no unusual outside dependencies and runs using standalone tools it fetches on its own through shell scripts. It should behave nicely on a freshly installed OS.

DBT runs and builds on:
* **Windows** - x86_64 only
    * *No need for WSL, cygwin, or mingw to be installed, and no weird things on the path. It's just cmd and powershell scripts to get started.*
* **MacOS (Darwin)** - 64 bit Intel x86_64 and arm64 (M1 & M2 native)
    * *`bash` will need to be on the path, even if it's not your shell of choice.*
* **Linux** - Intel x86_64 and arm64
    * *Linux depends on the presence of libxcrypt libraries, as directed in the kernel documentation (on Arch in particular, you may need to install the `libxcrypt-compat` package). `bash` will need to be on the path.*

To run DBT, simply open up a command prompt or your terminal/shell in your operating system of choice, change directory to the git repository you cloned earlier, and type the following variation of the command, depending on your operating system:

* Windows: `dbt`, `dbt.cmd`, or `.\dbt.cmd`
* MacOS (Darwin): `./dbt`
* Linux: `./dbt`

The first time you run DBT, it will check for the toolchain in the `toolchain` folder and if it's missing it will start the process of downloading and installing. Nothing will be installed or extracted outside of the repository directory so there is no worry of your system becoming polluted with dependencies. The toolchain includes:

* xPack GNU ARM Embedded GCC (v12.2.1)
* xPack OpenOCD (v0.12.0)
* xPack Clang (v14.0.6) (only `clang-format` and `clang-tidy`)
* xPack CMake (v3.23.5)
* xPack Ninja (v1.11.1)
* Standalone Python (v3.11.3)
    * SSL/TLS Certificates (certifi)
    * Multiple additional Python libraries from a frequently changing list

Ignore any warnings about certificates Python may express prior to installation of its certificates package.

Once installed, DBT should let you know you can request help with `--help` and then you're good to go.

DBT must always be run from the *root directory* of the repository. The remainder of the instructions will use the MacOS and Linux approach to calling DBT but substitute your own as needed.

### DBT Build Targets

These live under the `dbt build` subcommand. The general format is `dbt build {target} {configuration}`, with both target and configuration being optional (building all targets and configurations by default).

Built files output to subdirectories named after the configuration (e.g. `build/Release`).

#### Build targets:

* `all` - build all supported targets (7seg, oled)
* `oled` - build only for OLED.
* `7seg` - build only for 7SEG.

#### Build configurations:

* `debug` - build selected target with debugging support (including debugging symbols and different optimizations) 
* `release` - build selected target with optimizations for release.

#### Build arguments: 

* `-S` or `--no-status` - this disables the `ninja` status line while building. Output from compilation commands (such as warnings and errors) will still be printed.
* `-v` or `--verbose` - this prints greater level of detail to the console (i.e. exactly the compiler/linker commands called)
* `-c` or `--clean-first` - clean before building 

#### CMake custom arguments

Any additional arguments to CMake may be transparently passed via `dbt build`. Primarily this means `-j <jobs>` for limiting or defining the amount of parallelization. Most other relevant options are exposed via `dbt build`.


Additional detail on working with DBT will be coming soon.

---

## (Optional) e2 Studio Software (Windows/Linux)

### Download and install e2 studio
This is an Eclipse-based IDE distributed by Renesas, who make the Deluge’s Renesas RZ/A1L processor. You will have to create an account to access the download. It appears to only be available for Windows and Linux.

In the installer, you only need to install for the RZ device family:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/a4d57207-d4cb-4529-95a6-7f8cd21e0125)

Under “customise features”, you should make sure to install the “Embedded C/C++ J-Link Debugging” component:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/a725d3b9-59d0-4f2e-bb32-893c73850123)

Under “additional software”, you don’t need anything under the “Renesas Toolchains & Utilities” tab, so you can uncheck them all:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/497aa900-3c0b-4faa-b010-b29de774e78d)

But (still on the “additional software” step), under the “GCC Toolchains & Utilities” tab, you should select “GNU ARM Embedded 9.2.1 2019q4”, and “LibGen for GNU ARM Embedded”. Everything else can be unchecked:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/0d383e82-3b69-4fa2-89a1-538a03f897f5)

On Windows, you will get a number of permission request dialogues. You will also see a dialog asking you to locate the tool-chain directory. As part of the install process, the gnu subsystem will launch a command-line window with the working directory pointing at this tool-chain directory.

Those are all the custom options you need to select, and you can just complete the installation, run e2 studio, and select a workspace folder when it directs you - this is the folder under which you should place the Deluge source code inside its own subfolder, as shown below. Note that for the “hammer” icon/build function to un-gray, you must select the project at the highest level of the Project Explorer.
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/ff0ec17f-34ac-4701-8d55-b26811844aee)

### Building the firmware in e2 Studio
*(These instructions apply to the DelugeFirmwareCommunity repository, which features many helpful configuration additions.)*

To build a firmware binary from the codebase, first open any source file from the Deluge project in e2 studio, to select the Deluge codebase as the active project.

Then, click the dropdown arrow next to the “build” icon in the toolbar at the top. Make sure that the “e2-build-release-____” build configuration is selected based on whether you are building for the the OLED or 7-segment numeric display.
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/d3e71500-e327-46cb-9849-dcd3fcab1b4b)

Sometimes, making that selection will already trigger the build process. If not, you can do so by clicking the actual build icon (the hammer) itself, or by pressing ctrl+B.

The build process will take a few minutes, and you’ll see its progress in the console window.
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/16183fd5-d603-4ed8-b558-117c97bbba61)

The end result is the creation of a .bin file in the e2-build-release-oled or e2-build-release-7seg folder (depending which you selected above) which will have been created in the project’s directory. This file will get overwritten each time you do a build, so if you’ve made a build you’d like to keep, you should rename or copy the file.

You can now simply put this file on an SD card and install it on your Deluge as per normal.

# Direct firmware uploading and debugging
See: https://docs.google.com/document/d/1PkECgg0sxoVPng5CTdvRIZcY1CBBPHS8oVVhZIzTVn8/edit?usp=sharing
