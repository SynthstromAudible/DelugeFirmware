*You're looking at the Deluge Firmware official firmware repository, maintained only by Synthstrom Audible. If you're looking to get started with this codebase, we recommend you instead check out the DelugeFirmwareCommunity repository, which includes configuration changes which make it far easier to get up and running.*

# About
The Synthstrom Audible Deluge Firmware runs on the Deluge portable sequencer, synthesizer and sampler, produced by Synthstrom Audible Limited.

The Deluge Firmware’s codebase is written mostly in C++, with some low-level functions in C, and even the occasional line of assembly. The program is “bare-metal” - it runs without any higher level operating system.

The firmware runs on the Synthstrom Audible Deluge’s Renesas RZ/A1L processor, an Arm® Cortex®-A9 core running at 400MHz with 3MB of on-chip SRAM. The Deluge also includes a 64MB SDRAM chip.

The firmware is built using a GNU ARM Embedded GCC toolchain.

# Building the codebase
## Software installation

Download and install e2 studio - this is an Eclipse-based IDE distributed by Renesas, who make the Deluge’s Renesas RZ/A1L processor. You will have to create an account to access the download. It appears to only be available for Windows and Linux.

In the installer, you only need to install for the RZ device family:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/ece10af7-953b-4ff7-baba-cb30cdd5499a)

Under “customise features”, you should make sure to install the “Embedded C/C++ J-Link Debugging” component:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/8c05ed7f-4991-48c1-b8d0-18b7dbe92a7a)

Under “additional software”, you don’t need anything under the “Renesas Toolchains & Utilities” tab, so you can uncheck them all:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/540e4c05-ff25-4a81-a58b-2cd4f6629651)

But (still on the “additional software” step), under the “GCC Toolchains & Utilities” tab, you should select “GNU ARM Embedded 9.2.1 2019q4”, and “LibGen for GNU ARM Embedded”. Everything else can be unchecked:
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/a34ff0e0-9d91-4a37-924b-7f18d97dfeeb)

On Windows, you will get a number of permission request dialogues. You will also see a dialog asking you to locate the tool-chain directory. As part of the install process, the gnu subsystem will launch a command-line window with the working directory pointing at this tool-chain directory.

Those are all the custom options you need to select, and you can just complete the installation, run e2 studio, and select a workspace folder when it directs you - this is the folder under which you should place the Deluge source code inside its own subfolder, as shown below. Note that for the “hammer” icon/build function to un-gray, you must select the project at the highest level of the Project Explorer.
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/3751ce10-0209-403a-a97c-05f907f1d78e)

## Building the firmware
*(These instructions apply to the DelugeFirmwareCommunity repository, which features many helpful configuration additions.)*

To build a firmware binary from the codebase, first open any source file from the Deluge project in e2 studio, to select the Deluge codebase as the active project.

Then, click the dropdown arrow next to the “build” icon in the toolbar at the top. Make sure that the “e2-build-release-____” build configuration is selected based on whether you are building for the the OLED or 7-segment numeric display.
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/d458382d-ae10-4c93-88b3-a7e322f307ab)

Sometimes, making that selection will already trigger the build process. If not, you can do so by clicking the actual build icon (the hammer) itself, or by pressing ctrl+B.

The build process will take a few minutes, and you’ll see its progress in the console window.
![image](https://github.com/SynthstromAudible/DelugeFirmware/assets/1840176/34c80740-d01e-4c1b-a1a8-f19fcd8fc29e)

The end result is the creation of a .bin file in the e2-build-release-oled or e2-build-release-7seg folder (depending which you selected above) which will have been created in the project’s directory. This file will get overwritten each time you do a build, so if you’ve made a build you’d like to keep, you should rename or copy the file.

You can now simply put this file on an SD card and install it on your Deluge as per normal.
