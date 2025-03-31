---
title: Developer Tools
---

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

**NOTE:** loadfw _can not_ flash the new firmware to the permanent memory of the Deluge.
It is only loaded to RAM and then executed. Consequences:

- **Good:** After switching off and on again, Deluge will boot from the last firmware flashed to permanent memory _from the SD card_.
  This is great for development, because if your changes crash the Deluge, just switch it off and on again and you are instantly back to the last
  (hopefully stable) version that was flashed from SD card.
- **Meh**: If you want to keep your updated firmware on the Deluge persistently, i.e. surviving a power cycle, you _have to_ physically copy it to your SD card
  and update it as described in https://github.com/SynthstromAudible/DelugeFirmware/wiki#updating-firmware
- **Possibly bad:** _In rare cases_ the firmware can behave differently when uploaded via loadfw than when actually flashed from SD card.
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
