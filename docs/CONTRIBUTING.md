# How to contribute to DelugeFirmware

Above all, thank you for your interest in both the Synthstrom Deluge and being a contributor!

Please see also, the [Code of Conduct](CODE_OF_CONDUCT.md) document.

# Bug reports

Even if you are not a software developer you can contribute immensely by informing us about bugs and other problems you
notice during usage or testing of the firmwares created from this repository and related projects:

* Please submit an [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) highlighting as much *relevant*
  context about the bug as possible.
* Quality of information (rather than quantity, necessarily) is key!
  See [How to Report Bugs Effectively](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html) by Simon Tatham for more
  on this topic.
* Do let us know whether the bug was found on an OLED or 7seg Deluge as this may be important as well as the source and
  version of firmware (branch/etc.).
* Where the bug exists in build scripts, editors, or other development, building, and debugging processes, please
  provide excerpts of the relevant logs if you suspect they might be helpful.
* Bugs in related projects should be submitted to the appropriate issue tracker:
    * DelugeProbe: [litui/delugeprobe](https://github.com/litui/delugeprobe).
    * DBT-Toolchain: [litui/dbt-toolchain](https://github.com/litui/dbt-toolchain).

# Ideas, feature requests, suggestions and discussions

Please use the [Discussions](https://github.com/SynthstromAudible/DelugeFirmware/discussions) feature in our repository
which allows both creating discussion, ideas, polls and more as well as voting on existing topics.

* Please search first if there is already a (
  draft) [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls) or existing Discussion item related
  to your idea and start your discussion from there if possible.
* Reading, voting and commenting on discussion items and Pull requests will help a lot to improve the direction of the
  project.
* Please use the repository [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) tracker for problem
  reporting.
* For further inspiration and discussion there is also
  the [Deluge Suggestions area](https://forums.synthstrom.com/categories/deluge-suggestions)
  and [Open Source area](https://forums.synthstrom.com/categories/open-source-) on
  the [Synthstrom Forum](https://forums.synthstrom.com).
* For more in-depth technical discussion you are welcome to join the #dev-chat channel in
  the [Deluge User Discord](https://discord.com/invite/gWaJEh6pPP).

# Code review and testing

Pull requests that are no longer marked as Draft and are open for merging should be ready to test and any help is
appreciated.

* This includes both building and testing the firmware as well as reading and reviewing the source code.
* Please use the review and discussion functionality provided within GitHub.
* Code reviews are intended to increase quality and should not go into nitpicking, please remember
  the [Code of Conduct](CODE_OF_CONDUCT.md).
* Final decisions about merging of Pull requests is up to the code owners, see below and in [Governance](GOVERNANCE.md).

# Repository contributions

Everybody is invited to contribute to this repository as further outlined below:

* All contributions are handled with the [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls)
  mechanic in GitHub. If you start working on something please submit a Draft PR so everyone is informed (see below).
* Contributions can be anything that improves the project while fulfilling the requirements outlined below.
* As an inspiration here is a small and incomplete list of improvement areas: documentation, commenting complex code
  mechanics, refactoring (make code easier to read/improve clarity/make execution more efficient/improve
  maintainability/ease modification/improve structure), feature improvements, new features, easier accessibility,
  toolchain improvements, unit tests and many, many more.
* For information on the decision making process for accepting Pull requests please see below and also see
  the [Governance](GOVERNANCE.md) documentation.
* To keep the list manageable all Pull requests without author activity for longer than two weeks will be labelled "
  stale" and will be closed after another week. The author is welcome to submit a new PR for the same work at any time.

## Requirements

The following requirements must be fulfilled for a Pull request to be mergable to the `community` branch:

### General

* The contribution must be meaningful to the project. There must be a clear and articulate link between the change and
  an improvement for the developers, users or overall community.
* The Pull request must have a clear scope outlined in the description, please don't commit changes unrelated to the
  scope.
* Refactoring files changed for or related to the Pull request are encouraged. This includes improving code structure
  and file organisation.
* The description of the Pull request must also contain information on what functional areas have been touched and
  should be tested, ideally including a small test manual
* Appropriate branch name, if possible following standard conventions like git flow (e.g.
  feature/shiny_new_feature_name).
* No small Pull requests exclusively fixing single insignificant typos in code comments, one-off formatting mistakes or
  whitespace. Aggregate Pull requests fixing bigger areas can be accepted. (note: this is just for non-code fixes, fixing small bugs is encouraged!)
* Automated and low effort Pull requests will not be will not be tolerated or accepted (
  see [Hacktoberfest](https://blog.domenic.me/hacktoberfest/) for an example on why this rule is necessary).

### Code specific

* All project files, especially source files need to have a compatible license with the project.
  See [LICENSE](../LICENSE).
* There is no written standard on code guidelines yet but please make your code match the existing style as much as possible.
* Exception: the old code uses GOTOs and single returns heavily - new C++ code should favour other flow control methods 
  and early returns instead, the old code is a result of the project's roots in C.
* Clang Format runs on CI following the config in .clang-format. As there are slight differences between versions, we
  provide a command "./dbt format" to run the CI version exactly, which should gaurantee that your PR passes the checks
* All changes to the firmware have to be tested on a best effort basis to make sure they work as expected and don't
  break any existing functionality before submitting as ready to merge. This does not apply to Draft Pull requests.
* All changes need to be compatible with all available hardware variants, this currently includes OLED and 7-Segment.
* All changes need to be compatible with the currently official toolchain as described in the [Readme](../README.md).
* Acceptance of the continuous integration (CI) system is also required. It will automatically build open pull requests
  and check for compilation, formatting and in the future possibly unit testing.

### Application specific

* Pull requests that change how users can interact with the device or massively alter system performance (> 3% permanent
  cycle load) require either:
    * A runtime configuration setting that allows users to enable or disable the feature/change in behavior. See
      documentation on adding optional feature settings (Pull request #56).
    * Or if a runtime setting is not possible, a preprocessor switch that allows creating firmware without the change.
* Changes that massively increase image size (> 5% of total memory) also require a preprocessor switch starting with "
  FEATURE_" so they can be enabled or disabled.
* If the Pull request requires changes in structure of user files (e.g. project/synth or other xml files) or flash
  configuration:
    * It must ensure that files or flash configuration created with the official firmware or previous community releases
      stay compatible or are automatically upgraded (upward compatibility).
    * If possible files and flash configuration created with the changes of the Pull request can be used with older
      firmwares not having the change (downward compatibility). Older firmwares must not break if a newer configuration
      was stored on the device.
* If the Pull request changes end user behavoir or introduces new features, a new entry in
  the [CommunityFeatures.md](community_features.md) file needs to be created in the preexisting style describing the
  feature and its options as a small manual to users. This includes all runtime and compile time flags which shall be
  named in respective sections.

### UI Changes

Changes to the existing UI should follow the following process:

* Ensure they meet the guidelines set out in [UX Principles](ux_principles.md)
* Place the change in the community feature menu for one beta cycle
* Following a full beta cycle and user feedback, open a poll on the feature
* At the end of the beta cycle there will be a community meeting on Discord to discuss changes
* Given maintainer approval and positive feedback, the feature can be moved from the community menu to the default UI

## Workflow

Please follow the following steps for every pull request to ensure every contribution can be handled as frictionless and
transparent as possible:

1. Create a [Draft Pull request](https://github.blog/2019-02-14-introducing-draft-pull-requests/) including a
   description on what will be changed and what impact you expect. The title can start with "[Draft] " during
   development.
2. Work on the Pull request
3. Before a Pull request can be considered ready, all upstream changes from the `develop` branch need to be merged into
   it. It is the duty of everyone to help make merging into `develop` as painless as possible so please try to align if
   you see that your Pull requests works in a similar area as another one.
4. Once the Pull request is ready, fulfills all requirements outlined above, and is up to date with the `develop`
   branch, it can be converted from Draft and marked as ready for review.
5. Having multiple reviews for every Pull request would be nice. Reviews from community members not mentioned in
   the [CODEOWNERS](../CODEOWNERS) file should be taken seriously and used as an important source of feedback, but have
   no decisional power on what gets merged into the `develop` branch.
6. At least one member of the [CODEOWNERS](../CODEOWNERS) file needs to review every pull request while also considering
   community reviews in their decision.
    * CODEOWNERS can decline merging a Pull request if it does not fulfill the requirements outlined above. They need to
      give clear feedback on which requirements have not been met and also provide an opportunity to improve the Pull
      request to meet the requirements within reasonable boundaries (e.g. there are limits on how much work can be
      expected from a CODEOWNER for a specific Pull request).
    * If one or more CODEOWNERS are sure the requirements have been met, they will merge the change into the `develop`
      branch.
    * For more information about governance and handling of decisional matters please take a look at
      the [Governance](GOVERNANCE.md) document.

In addition to this workflow it is not a requirement but would be nice if developers could help the maintenance of the
project by:

* Doing housekeeping of the Pull request including processing community feedback.
* Tagging the pull request appropriately where possible.
* Trying to keep some sense of ownership over the touched areas of the codebase and provide support in case of problems,
  questions or future developments.

## Tools

To help you to speed up your development you have two command line tools that can help you streamline the process of
loading a firmware and then debugging it. We also have a Deluge Crash Reader Discord Bot in the nightly-testing channel
of the Deluge discord.

### IDE Configs

Suggested VSCode configs are in IDE_Configs/vscode. These will configure building, linting, flashing, and debugging for
a variety of possible debug hardware.
If you'd like to use our suggested config, just copy the folder from IDE_configs/vscode to .vscode in the repo root.
.vscode itself is gitignored so you can modify it as needed to fit your desired workflow later.

### Loading new firmware via USB (loadfw)

To use this feature, you will need to first flash a build via the SD card that has been built with `ENABLE_SYSEX_LOAD=YES`.

```shell
./dbt configure -DENABLE_SYSEX_LOAD=YES
./dbt build release
# copy build/Release/deluge.bin to an SD card and flash it
```

You can load the firmware over USB. As this could be a security risk, it must be enabled in community feature settings
on the Deluge: `COMMUNITY FEATURES > ALLOW INSECURE DEVELOP SYSEX FEATURES`, after enabling you will see a key that you
need to use in the command to authenticate with the Deluge.

`./dbt loadfw release` (Or whichever build you want to upload instead of "release".)

The `loadfw` command should automatically identify the correct MIDI port, as long as you are connected directly via USB.

The first time you run it, the command will ask you for the key code you from the Deluge menu. The code is cached in
`.deluge_hex_key` file in the DelugeFirmware directory, if it changes delete the file to have `loadfw` ask again for the key. 

More instructions available via:
`./dbt loadfw -h`

**NOTE:** loadfw *can not* flash the new firmware to the permanent memory of the Deluge. 
It is only loaded to RAM and then executed. Consequences:
* **Good:** After switching off and on again, Deluge will boot from the last firmware flashed to permanent memory *from the SD card*.
This is great for development, because if your changes crash the Deluge, just switch it off and on again and you are instantly back to the last
(hopefully stable) version that was flashed from SD card.
* **Meh**: If you want to keep your updated firmware on the Deluge persistently, i.e. surviving a power cycle, you *have to* physically copy it to your SD card
and update it as described in https://github.com/SynthstromAudible/DelugeFirmware/wiki#updating-firmware
* **Possibly bad:** *In rare cases* the firmware can behave differently when uploaded via loadfw than when actually flashed from SD card. 
While this should only be relevant when changing low-level code executed at boot time,
**please** always verify that your firmware also works when actually flashed from SD card,
especially before making a pull request (ask me how I know...). 

### Printing debug log messages on the console

While the Deluge is connected over USB, you can print the messages it produces to the console. In order to do that
execute this command:

`./dbt sysex-logging`

Same as with `loadfw`, the command should auto-identify the correct MIDI port.

More instructions available via:
`./dbt sysex-logging -h`

Release builds do not send debug messages over sysex, so you need to build and fliash either "relwithdebinfo" or "debug".

To make debug log prints in your code, which will be sent to the console, here is a code example:

```
#include "io/debug/log.h" // at the top of the file

D_PRINTLN("my log message which prints an integer value %d", theIntegerValue);
```

### Useful extra debug options

Using the previously mentionned sysex debugging, the following option can be toggled to enable pad logging for the pad matrix driver:

```shell
./dbt configure -DENABLE_MATRIX_DEBUG=YES
./dbt build debug
# copy build/Release/deluge.bin to an SD card and flash it
```

This is a sample of the output:
```
@matrix_driver.cpp:71: UI=instrument_clip_view,PAD_X=17,PAD_Y=3,VEL=255
@matrix_driver.cpp:71: UI=instrument_clip_view,PAD_X=17,PAD_Y=3,VEL=0
@matrix_driver.cpp:71: UI=instrument_clip_view,PAD_X=17,PAD_Y=2,VEL=255
@matrix_driver.cpp:71: UI=instrument_clip_view,PAD_X=17,PAD_Y=2,VEL=0
```

### Deluge Crash Reader Discord Bot

If deluge crashes, there is a colorful pixelated image that gets displayed across the main pads and sidebar. In case 
of a CPU fault or C++ exception, the handler will print the last link register address and as many pointers to the code 
section it can find in the stack data. The main pads display 4 32-bit numbers which are the addresses. Every color block 
is one address. Each pad is a bit, 1 is lit, 0 is unlit. The most significant bit is at the lower left corner
of the color block, and the least significant bit is at the top right. Each column is one byte, reading along a single 
column from bottom row to top row first, then incrementing to the next column from left to right. Rotating Deluge 90 
degrees to the right makes it easy to read and enter the bytes into Windows calculator. However, we have created a 
Deluge Crash Reader Discord Bot which can decode the addresses for you from a picture of the pads.

The red sidebar is the first 4 chars of commit ID. Pink color in main pads means it's from ARM USR mode. Blue would be
SYS mode. Stack pointers will alternate green and light blue. 

The Deluge Crash Reader Discord Bot uses image recognition to decode the pad lights for you if you post a picture of 
your Deluge to the nightly-testing channel on the Deluge Discord. Take a photo of your deluge, make sure all the pad 
LEDs are inside the photo, then post it in the channel, and the robot should output the decoded addresses and the 
command used to investigate the addresses.

For example, after posting a picture, the Deluge Crash Reader might output a message similar to this:

Thanks for the image @username!, try running  
I couldn't find a recent matching commit for `0x714a`  
`arm-none-eabi-addr2line -Capife build/Release/deluge.elf 0x200a2f5b 0x200f3100 0x200fe6c1 0x2006dfe7 `  

Then run the command that Deluge Crash Reader Bot outputs. This outputs the latest stack trace before hard crashing.
(If you're on Windows and using VS Code, try running `.\dbt.cmd shell` first in your terminal.)

[Black Box PR](https://github.com/SynthstromAudible/DelugeFirmware/pull/660)
[Discord Bot Repo](https://github.com/0beron/delugeqr/)

