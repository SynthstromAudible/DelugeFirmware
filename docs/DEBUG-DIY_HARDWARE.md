# The Hitchhiker's Guide to DIY Deluge Debugging Hardware

by *Aria Burrell \<litui@litui.ca\>*

---

> **DON'T PANIC**

---

## Table of Contents

1. [Introduction](#introduction)
    1. [Hardware Debuggers](#hardware-debuggers)
    2. [Choose Your Own Debugging Adventure](#choose-your-own-debugging-adventure)
        1. [The Shorter (but Pricier) Path](#the-shorter-but-pricier-path)
        2. [The Longer (but MUCH Cheaper) Path](#the-longer-but-much-cheaper-path)
    3. [Prerequisites (Stuff to Acquire)](#prerequisites-stuff-to-acquire)
        1. [Tools You'll Need](#tools-youll-need)
        2. [Nice-to-haves](#nice-to-haves)
        3. [Specific Components to Purchase](#specific-components-to-purchase)
        4. [Optional Parts](#optional-parts)
4. [Adding a Debug Header to Your Deluge](#adding-a-debug-header-to-your-deluge)
    1. [Part I: Unscrewing Everything](#part-i-unscrewing-everything)
    2. [Part II: Soldering the Debug Pins in Place](#part-ii-soldering-the-debug-pins-in-place)
    3. [Part III: Finishing up](#part-iii-finishing-up)
5. [Modding Your Deluge Enclosure (Optional)](#modifying-your-deluge-enclosure-optional)
    1. [Part I: Making the Debug Umbilical Cord](#part-i-making-the-debug-umbilical-cord)
    2. [Part II: Wiring the SWD Breakout Board](#part-ii-wiring-the-swd-breakout-board)
    3. [Part III: Making a Hole in Your Chassis](#part-iii-making-a-hole-in-your-chassis)
    4. [Part IV: Putting It All Together](#part-iv-putting-it-all-together)
6. [Building and Flashing Your DelugeProbe](#building-and-flashing-your-delugeprobe)
    1. [Part I: Adding a Debug Connector](#part-i-adding-a-debug-connector)
    2. [Part II: Flashing the DelugeProbe Firmware](#part-ii-flashing-the-delugeprobe-firmware)

## Introduction

So, you're a developer and synth enthusiast who is interested in contributing to the Deluge now that it's gone Open Source... welcome! Especially if you're new to embedded development, it can be a bit daunting at first, but this guide aims to start you off with the right gear!

This guide is intended primarily for those who are soldering at an **intermediate** level. Most of the process is very basic for those experienced with circuitry and embedded systems work (or even DIY synth modules), but for absolute beginners there are some difficult parts where you should enlist the help of a skilled solderer.

Be aware that if you follow any of these physical modding steps, regardless of your degree of success: 

![This Will Void Your Warranty](assets/VoidYourWarranty.jpg)

**Synthstrom waives all liability for damage incurred through the following of this process.**

**But**, you're an intrepid developer and/or synthesist who laughs in the face of adversity (*"ha ha ha!"*)! You got this, and if you're at all uncertain about any of these steps, by all means come ask for community help in the forum or in the *#dev-chat* channel on the *Synthstrom Audible* Discord.

### Hardware Debuggers

Writing, compiling, and loading of firmware (via SD Card) *doesn't require any special hardware*. In the case of small code tweaks you can probably get by making and testing the change over a few iterations without special hardware.

Once things start getting *complicated* (e.g.: you need to track changing variable values, test function outputs, etc.), you're going to want a hardware debugger.

### Choose Your Own Debugging Adventure

There are two main paths you can take through this guide, depending on your situation, with more paths likely to open up as development matures.

#### The Shorter (but pricier) Path

*IF* you use Windows and already own a [SEGGER J-Link](https://www.segger.com/products/debug-probes/j-link/) or don't mind spending between **$500 USD** and **$1500 USD** on a small, single-purpose piece of debugging gear, *THEN* you have the easier path: you can follow the **Debug Header Installation** instructions below, hook up your J-Link, and skip the rest of this guide.

> Note that while you can acquire a far less expensive "Edu"/Educational version of the J-Link, its license does not cover the development of software for a commercial product.

The **[e<sup>2</sup> Studio](https://www.renesas.com/us/en/software-tool/e-studio)** software (based on Eclipse) is the official (Windows) tool Renesas recommends for development and debugging on the Deluge's microprocessor. Everything JLink-related should work out-of-the-box in **e<sup>2</sup> Studio**.

#### The Longer (but MUCH cheaper) Path

*IF* you use Linux or Mac, already own a [DAPlink debugger](https://daplink.io/), or have $5 in your back pocket you're willing to spend on a [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) which can easily be converted to a **DelugeProbe** (CMSIS-DAP) debugger, this is the path for you!

Rather than spending almost as much as you spent on your Deluge buying a debugger, you could spend a maximum of about $32 USD (all inclusive) plus a couple hours of your time to have everything you need to contribute to the Deluge open source project using your preferred IDE/Editor and operating system.

Interested? Read on. This whole guide is for you.

### Prerequisites (Stuff to Acquire)

Hardware debugging is going to require you to have access to the JTAG/SWD debug header on your Deluge. This means at minimum you will need to open up your Deluge and solder in an SWD (Serial Wire Debug) connector. If you'd like to be able to debug with your Deluge sealed up, you will also need to modify the body of the Deluge. Instructions below!

#### Tools You'll Need:

Basic tools and supplies you will almost certainly need if you aim to complete all the steps (debug pins, optional case mod, DelugeProbe):

* a clean, freshly tinned precision soldering iron
* thin gauge flux-core solder wire for fine electronics work
* a few different screwdrivers for opening and closing the chassis:
  * full size Phillips (X) #2
  * precision Phillips (X) #1
  * 1.5mm hex driver
* wire cutters (or scissors)
* wire strippers
* needlenose pliers and/or an adjustable wrench (for removing nuts holding on the faceplate)
* a Micro USB cable for connecting your computer to the Raspberry Pi Pico

#### Nice-to-haves: 

* a divided tray for temporarily holding screws and components until needed
* an anti-static mat and/or bracelet
* a Dremel or other small metal-drilling/cutting/engraving apparatus to cut an access hole for the debug port
* electrician's tape (for covering up contacts and patching errors)
* scotch or masking tape (for holding pins in place while you solder)

#### Specific Components to Purchase

Whether or not you are the one doing the actual work, you will need the following components to complete all the mod steps in this guide, priced out below (in USD):

| **Qty.** | **Mouser**                                                                       | **Description**                                                              | **USD** Each |
|----------|----------------------------------------------------------------------------------|------------------------------------------------------------------------------|--------------|
|          | **For debug pinout Deluge mod:**                                                   | *(on the board only; Deluge case must be left partially open to use)* |              |
| 1x each  | [200-FTS10501LD](https://mou.sr/43cyZh5)                                         | Samtec Micro Low Profile Header Strip (*ignore the photo*)                   | $2.62        |
|          | **For (optional) Deluge case mod:**                                                   | *(requires external modification to case, but allows access even if chassis is closed)* |  |
| 1x each  | [485-1675](https://mou.sr/3IIZnqR)                                               | Adafruit 10-pin 2x5 Socket 1.27mm pitch IDC (SWD) Cable                      | $2.95        |
| 1x each  | [485-2743](https://mou.sr/43xAPcu)                                               | Adafruit SWD (2x5 1.27mm) Cable Breakout Board                               | $2.95        |
| 1x tube  | [910-EGS10C-20G](https://mou.sr/43qNPkx)                                         | Electronics Grade Silicone Adhesive Sealant (Clear) 20g Squeeze Tube         | $4.94        |
|          | **For Making DelugeProbe Debugger:**                                               |  |              |
| 1x each  | [485-1675](https://mou.sr/3IIZnqR)                                               | Adafruit 10-pin 2x5 Socket 1.27mm pitch IDC (SWD) Cable            | $2.95        |
| 1x each  | [485-2743](https://mou.sr/43xAPcu)                                               | Adafruit SWD (2x5 1.27mm) Cable Breakout Board                     | $2.95        |
| 1x spool | [485-4730](https://mou.sr/3N0l5Jh)                                               | Adafruit 30AWG Rainbow wire wrap (*way more than you'll probably ever need*) | $6.95        |
| 1x each  | [358-SC0917](https://mou.sr/3LXGxhT) **OR** [358-SC0915](https://mou.sr/41eZOjm) | Raspberry Pi Pico (*with or without pre-soldered pins*)                      | $5.00        |
|          |                                                                                  | **GRAND TOTAL**                                                              | **$31.31**   |


There *are* other parts you could use if you have different things on hand, and cheaper (bulk) supplies you *could* stock up on if you're an electronics technician or engineer but this guide is intended as a one-off. Remember it never hurts to get a couple extras of something in case of a parts defect or mistakes being made!

#### Optional Parts

If you prefer your electronic parts to be stably mounted, consider purchasing a mixed pack of *protoboards* from [Amazon](https://www.amazon.ca/DEYUE-Double-Sided-Prototyping-Solder-able-Protoboards/dp/B07JLC4R2R/), [AliExpress](https://www.aliexpress.com/item/1005004483271011.html), or even a dollar store. The instructions below are the same whether or not you use a protoboard.

If you would like to house your debugger in some sort of case, lots of options are available on Mouser and other sites.

Personally, I am a fan of putting my Picos on [DFRobot IO Expansion Boards](https://mou.sr/45CTwNq). This exposes *labelled* male and female hookups for every pin so I don't need to solder anything to the board (allowing me to repurpose it for other things at any time). I then use an [Adafruit JTAG to SWD Adapter Board](https://mou.sr/3oG18Oz) to connect the Pico DelugeProbe to the Deluge. It looks like this:

![Litui's Deluge Debug Setup](assets/LituiPicoDebugSetup.png)

The guide below will do things slightly differently, soldering connections to the DelugeProbe and making it more permanent (and less prone to user error).

## Adding a Debug Header to your Deluge

*The soldering skill required below is **Intermediate** in difficulty so if you are new to soldering it might be beneficial to ask someone with more soldering experience to help out with the more difficult parts. It's only one section that is hard, the rest is pretty basic.*

The Deluge will need to come apart almost completely and components will be vulnerable to static shock and damage throughout so please take care when the Deluge case is open.

**THIS WILL PROBABLY VOID YOUR HARDWARE WARRANTY**

You agree that by following the below process you understand the risks and that *Synthstrom Audible* is not liable in the event of damage.

If this is a major concern for you, check with **Ian** at *Synthstrom* if they are currently offering a service to install debug pins for you.

### Part I: Unscrewing everything

Prepare table space (enough to fit three Deluges) and ready your parts tray as there will be many parts to keep track of. In addition to your anti-static mat, you will need the Deluge to be face-down for part of this so find a book at least as thick as the height of the knobs to rest the keybed of the Deluge on.

[![Deluge Takedown Video](assets/InsideTheDelugeVideoSnap.png)](https://www.youtube.com/watch?v=M5Go-ReaJEw "Video: Inside the Deluge")

Follow the steps in the YouTube video linked above to take the Deluge apart. Please note the creator's correction that you should use a hex precision-driver, not a torx driver.

Don't be afraid to take pictures at each stage so you can remember how things looked when it comes time to put it all back together. Take your time, be methodical, and don't forget to breathe.

### Part II: Soldering the Debug Pins in Place

Now that the guts of the Deluge are exposed you'll need to locate the debug header. It will be 2 columns of 5 rows each, located above (or below depending what way you're looking at it) the Renesas processor.

![Locating the Debug Header](assets/FindDebugHeader.png)

Before getting started, I recommend watching through 
the following video on soldering the identical 2x5 set of pins to an ARM development board SWD header. 

[![Video ](assets/SolderingVideoThumb.png)](https://www.youtube.com/watch?v=ZSaXAccnQPs "Video: Inside the Deluge")

Once located you'll need to insert the short-pinned (unplated) side of your 10 pin strip and use a piece of scotch or masking tape *temporarily* to hold it in place. When you *gently* flip the board over, you should see the pins peeking through the holes on the other side of the board.

![10 pin header soldering result](assets/FrontOfDelugeSoldered.png)

You'll notice the holes are very closely positioned next to each other as compared to other headers on the board (for instance, P52 to the side of the processor). This makes for some difficult soldering to ensure there is no current flowing between pins.

If you will be soldering yourself, ready your clean, *tinned* soldering iron. Gently, but quickly apply heat to each pin/pad and melt a very small amount of solder and flux on each. 

You absolutely don't want the solder to spread across pads, you just want it to bond each pin to the pads around each hole. Take care not to apply heat for too long. If it doesn't work within a couple seconds, stop. Wait for the board to cool, then try again. In the end it should look something like this:

![Close-up soldering result](assets/ClearSeparationBetweenPads.png)

It doesn't have to be perfect, but at a minimum, there **must** be clear separation between joints and good connections between each pin and its pad. Use a magnifying glass to check if needed. If you mess up, there are a couple of tricks to cleaning up between pads:

* Remove any solder from your iron's tip by wiping it on the wet sponge included in your kit, then slowly, without much pressure, push the heated tip against the space between pins allowing it to melt and slide through the accumulated solder. If it doesn't work the first pass, wipe off the iron and try again - a well-tinned tip will attract excess solder to it.
* Get yourself a [solder vacuum](https://mou.sr/45Dn6Cw), melt the blob then immediately press the vacuum to the excess solder and press the trigger. It may over-clear the mess, but you can always apply additional solder to the pins.

Once complete, on the other side, it will look something like this, (though if you ordered from the parts list above, your pins will *not* include a plastic shield - *through-hole versions of this part were hard to source*):

![Soldered-in pins with notched shield](assets/NotchWhiteArrow.png)

### Part III: Finishing up

Now that you've visually ensured your pins are soldered correctly, you'll need to ensure all the rubber keycaps are in place and screw the board back onto the frontplate.

If you'll be modding the case, take a break before starting the next part (you deserve it!).

If you're *not* going to be modding the case, connect one of your debug cables to the pins as in the following image, and put the rest of your Deluge enclosure back together. I suggest leaving off the left wooden panel/ear and letting the cable dangle out that side so it can be connected to the probe easily.

![Align red/pink wire with the white arrow](assets/WireLineUpWithCable.png)

Regardless of whether you have a notched shield on your pins, you need to make sure when attaching the cable that you align the red/pink wire with the white arrow on your Deluge board. The wire may be a different colour but it will be unique as compared to the others in the ribbon.

## Modifying Your Deluge Enclosure (optional)

**THIS WILL DEFINITELY VOID YOUR HARDWARE WARRANTY**

There are multiple routes you could take to exposing a debug port on the outside of your Deluge. I chose to mod the metal backplate but it occurred to me after the fact that I could have used my CNC router and done a tidier job modding the left wooden panel/ear (if anyone wants to try that and provide CAD/CAM files, send them my way and I'll add them to the guide).

Regardless of *tidiness*, my approach works quite well day-to-day.

You should already have a 2x5 set of pins soldered into your Deluge board, the board should be screwed back on to the frontplate, and the remainder of your Deluge enclosure should be open.

That being the case, we'll ignore the Deluge for the first part of this, focusing instead on one of the debug cables and one of the Adafruit SWD breakout boards.

### Part I: Making the Debug "Umbilical Cord"

For this part, you will need one of the Adafruit SWD breakout boards purchased above (the other one is for making the DelugeProbe later) and one of the 10 pin Adafruit SWD cables.

![Cut the Cable](assets/CutTheCable.jpg)

The first step is to cut off **one** of the cable's connectors. Ensure your cut is at 90 degrees and very close to the connector so you leave enough length for the cable to reach. You want to leave the other end intact so it can be attached to the pins now soldered onto your board.

Once your cable is cut, you'll want to *carefully* split the wires at the end apart so they can be separately connected to the breakout board. You can use an Xacto blade or other sharp implement to help with this process but try not to cut into the insulation around the wires, just the thin material holding the separate bundles together. The separated lengths should be between 2cm and 3cm (3/4 inch to 1 inch).

![Splitting the wires with an Xacto blade](assets/XactoSplitCable.jpg)

![Fanned wires on debug cable](assets/FannedWiresOnDebugCable.jpg)

Once separated, you'll need to strip the ends of the wires no more than a few millimetres (1/8 inch).

> If you don't have a wire stripper or a set of snips with a *notch* for stripping wire, you can slide a blade gently around the wire to gently mark the insulation. Then using your fingernails or a set of pliers, you should be able to pull off the insulation beyond your mark. This takes some practice so perhaps try with other wire first.

![Stripped debug cable wires](assets/StrippedDebugCable.jpg)

Now you'll need to "tin" each of the exposed ends of each wire by applying a thin layer of solder to each. This improves adhesion and reduces the risk of corrosion. Find a way to keep the cable steady while you work -- electronics folks often use a "helping hand" but you can easily hold the cable down with your wrist. If using a *helping hand* or clip, I recommend wrapping the cable with a spare bit of plastic bag or tape so the clip doesn't cut into the soft insulation.

![Soldering the tips](assets/SolderingTheTips.jpg)

Keeping each of the wire ends bundled (you don't want stray bits of wire sticking out), apply solder to each tip. Once complete, grab your SWD breakout board and have a quick stretch before we get to the next part.

### Part II: Wiring the SWD Breakout Board

![Numbered wires and breakout board pads](assets/NumberedWiresAndBreakout.png)

Using the above as a reference, take note of how the solder points for each wire on the breakout board are *alternating*. Odd numbers down the one side starting at `Vref` (the red wire), and even numbers down the other side ending in `RST`.

Using a vise or helping hand to hold onto the SWD breakout board, solder each wire in place, remembering to alternate sides as in the diagram.

![Breakout Board with Helping Hand](assets/TheBreakoutBoardWithHelpingHand.jpg)

When complete, it should look something like this:

![Breakout Board Soldered from the Back](assets/BreakoutBoardSolderedRear.jpg)

Now get hold of your Deluge enclosure because you're ready for the scary part...

### Part III: Making a Hole in Your Chassis

Here, you have multiple options as there is a lot of free space on the back panel or the left wooden ear. I opted to measure out the needed space and drill/file the way with a Dremel engraver, but you may have cleaner/more accurate tools at your disposal. My recommendation is to cut a space for the jack below the headphone jack. This way, on the off-chance that it causes any interference it'll only be for you as the listener, not on the inputs or line-outs.

![Litui's Janky Case Mod](assets/JankyCaseMod.jpg)

If you make a bit of a mess, it's not a big deal as you can slap a sticker over it (as I did), but there are cleaner approaches. 

![Back Panel with Sticker](assets/BackPanelSticker.jpg)

I would recommend putting masking tape over the area you'll be cutting temporarily. Not only will you be able to trace the lines you'll need to cut but you'll avoid making unsightly marks on the surface.

Did I follow that advice? No, but I also made a bit of a mess. Your mileage may vary.

### Part IV: Putting It All Together

Now that you have a hole that the connector on the breakout board fits through, crack open that electronics-grade silicone tube you purchased, and apply liberally to both the soldered (top) side of the breakout board and the inside of the Deluge case around where the connector will go. *Avoid* getting silicone inside the connector if at all possible as it will be a pain to get out once cured.

Push the connector through the hole and let the connector and back panel sit for 24 hours to ensure the breakout board doesn't go anywhere. Make sure you cover up your open Deluge with something (perhaps your solder mat) and keep pets and dust away until you're ready to work with it again.

...

After your wait, get ready to put the Deluge back together again, but this time make sure to attach the debug cable to the pins you installed before, as below. Remember, **red wire to white arrow** or, as is visible in the image, red wire *away* from the Renesas chip. Make sure the connector is on properly (over all 10 pins) and pressed down such that it will not move in a closed case.

![Back Panel Siliconed and Attached](assets/BackPanelSiliconedAndAttached.jpg)

To close up your case, make sure the silicone pads are properly aligned on the PCB -- most of them have little nubs that sit in the PCB to help with this. Pay partiular attention to the holes through the PCB that will be used to join it to the faceplate! If some of the silicone pad ends up between the PCB and the faceplate standoff, the PCB will be bent (not permanantly) and the pads will feel extra-mushy when you press them. Then you should be able to drop the faceplate on top and press the plate and PCB together. Now put all the screws and knobs back on, and fire it up to make sure everything still works.

Congrats, you're done with the hardest parts!

## Building and flashing your DelugeProbe

![First version of the DelugeProbe](assets/DelugeProbev1.jpg)

The Raspberry Pi Pico is an inexpensive development board that, among many other useful things, can be used as a hardware debugger. The Picoprobe firmware, intended for using a Pico to debug other Picos primarily, also works reasonably to debug other ARM-based devices. **DelugeProbe** is a custom firmware based on a fork of the Picoprobe firmware called "Yet Another Picoprobe". YAPicoprobe includes a variety of features, most of which have been disabled in the **DelugeProbe** for simplicity's sake.

**DelugeProbe** is a work-in-progress firmware being tailored to achieving as-fast-as-possible debugging of the Synthstrom Deluge and its Renesas RZ/A1LU microprocessor.

**DelugeProbe** Additional Goals (not yet working):
* Drag & Drop Firmware Loading for the Deluge
* At-a-glance Deluge status information from running memory
* Support for other, more compact RP2040-based boards including the Adafruit QT Py RP2040, the Adafruit KeeBoar RP2040, and the Seeed RP2040.

### Part I: Adding a Debug Connector

This section involves only *basic* soldering. If your pico has pins, you may want to use a protoboard to hold everything (as in the wiring diagram below), but this wiring works just fine loosely assembled as well:

![Wiring diagram](assets/DelugeProbeWiring.png)

* Pico GND -> breakout board GND & GND (solder/wire these together)
* Pico GP2 (4th pin) -> Breakout board CLK
* Pico GP3 (5th pin) -> Breakout board SWIO
* Pico GP4 (6th pin) -> Breakout board NC
* Pico GP5 (7th pin) -> Breakout board SWO

This wiring is the same whether you're using a Raspberry Pi Pico or Pico W.

Once wired up, you'll be able to connect the debug cable between your Pico and the Deluge. But first, you need some firmware!

### Part II: Flashing the DelugeProbe Firmware

Start by downloading the latest [DelugeProbe .uf2 firmware](https://github.com/litui/delugeprobe/releases/latest). Save it somewhere easily accessible for drag & drop on your computer.

You can also use the [PicoProbe firmware](https://github.com/raspberrypi/picoprobe/releases/latest) or [YAPicoProbe firmware](https://github.com/rgrr/yapicoprobe/releases/latest) if you choose, though your debugging experience may be less tailored to the Deluge's specific hardware if you go with either of these. There are also notable speed improvements in YAPicoProbe and DelugeProbe over the original PicoProbe firmware.

Hold down the **Reset**/**Bootsel** button on the Raspberry Pi Pico and, while continuing to hold the button down, plug it in to your computer via its Micro USB port. Once plugged in, wait a moment then let go of the button.

If a window doesn't pop up for it, check your system to see if you have a new "drive" called `RPI-RP2`. Open up that directory/folder. In another window, find where you put the `.uf2` file for the DelugeProbe firmware (try your `Download` folder), and simply drag it to the `RPI-RP2` drive.

Once loaded, the drive will disappear and, if successful, be replaced by a drive called DelugeProbe.

Congratulations! You've got a DelugeProbe. Out of the box, it should work with your Deluge and the Renesas e<sup>2</sup> Studio configuration included in the DelugeFirmware git repository.

Instructions to use it with other approaches will be added to this document in the fullness of time.
