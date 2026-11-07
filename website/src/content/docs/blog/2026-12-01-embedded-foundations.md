---
title: "Embedded Foundations"
date: 2026-12-01
draft: true
authors:
  - Katherine Whitlock
---

<!-- Prompts like this one are HTML comments, so they never appear on the page. -->

## The Foundation

Have you ever looked at a synthesizer or sampler or Eurorack module and wondered "Wow, there's a lot of stuff going on there, how does it know what I'm doing and how I'm interacting with it?" If so, then you're in luck. If not, then maybe this will give some insight anyways, so that in the future you'll know how this all works.

In modern embedded devices, such as synthesizers, samplers, keyboards, controllers, Eurorack modules, etc., it's very rare that whatever does all the audio processing is written so specifically for a processor that it's manually poking all the memory addresses and registers to communicate with all the buttons, switches, knobs, and screens that may exist.

Instead, this is often left to a dedicated platform layer, allowing some level of abstraction between whatever runtime or application code may exist and the hardware itself. This increases portability and ease of development, while also allowing for better separation of concerns.

For example, many devices run on the Arduino framework. This framework is responsible for knowing details of both the CPU, and (to some degree) the board, and then presenting an easy-to-use facade over top which allows fairly easy portability between various Arduino boards. It is a relatively simple endeavor to port a program written for an older Uno R3 to a Mega or even an Adafruit Metro.

![One line of Arduino code, digitalWrite(LED_BUILTIN, HIGH), runs unchanged on an Uno R3 and a Mega 2560, which use AVR chips, and on an Adafruit Metro M0 Express, which uses an ARM chip. The framework maps pin 13 to port pin PB5 on the Uno, PB7 on the Mega and PA17 on the Metro.](../../../assets/blog/what-the-deluge-stands-on/arduino-boards.svg)

_One line of code, three boards and two kinds of processor. The framework knows which port pin is pin 13 on each board, so the sketch never has to._

## How things work today

On the Deluge, this layer originally started out as Renesas' own vendor platform code and drivers, and became entangled with the application code as it was further developed. This is evident in the way that both the RZA1 platform code and the application call into each other, and the way that the application does things such as reading raw bytes from the PIC or interacting with the SD routine.

Essentially, the Deluge is married to its hardware by nature of this entanglement, and in order to get it running on anything else (such as x86 or ARM64), this had to be remedied.

The solution to this was to start from scratch: an entirely new set of layers, written in Rust for safety, running on top of the Embassy runtime framework, and abstracting away all the platform details and drivers that an application shouldn't need to know to run.

Ultimately this required recreating most of Renesas' drivers, and even writing new ones, that were up to spec and matched what we needed.

<!--
- On main, the application talks to the hardware directly.
- 55 app files include RZA1/ hardware headers: UART most often (25 includes), then system (13) and OLED (11).
- deluge.cpp reads raw PIC bytes with uartGetChar. If the SD routine is busy, it pushes the byte back with uartPutCharBack and tries later.
- The app can't run anywhere except on a Deluge, so it can only be tested on one.
-->

![Before and after map. On main, the sequencer, sound engine, UI and project code share one program with the hardware drivers, task scheduler, FatFS filesystem, fault handler and heap cores. On next, the first four stay in the C++ app above libdeluge, and the other five moved below it into the Rust platform.](../../../assets/blog/what-the-deluge-stands-on/what-moved.svg)

_What sits on each side of the contract. The musical parts of the firmware stayed in the app. The machinery underneath them moved below libdeluge._

## So what are a HAL and BSP anyways?

A "HAL" or "Hardware Abstraction Layer" acts as the first real layer between an application and silicon. It's typically intended for a single CPU model or a CPU family, and has details about which memory locations correspond to different peripherals. In the case of Rust, it also acts as a safety barrier, presenting a safe "owning" API that works in conjunction with Rust's borrow-checker to hide the messy unsafe details of poking raw registers and memory locations.

A "BSP" or "Board Support Package" is what it sounds like on the tin: if the HAL abstracts details of a CPU, then the BSP does the same for an entire board. This includes things like drivers for whatever I2C, SPI, and UART devices may be connected to the CPU, and also simplifications for interacting with them. This is also where a lot of aliasing and binding happens, so that instead of saying "turn on the LED on Pin 13 in inverted mode", you can instead say "turn on the SYNC LED".

Together, these two layers act as a way to hide details inside them. Whereas previously, if you pressed a button the Deluge, the application would read raw bytes directly from the UART peripheral and queue up events and handle the appropriate response itself before sending out raw color bytes, instead that behavior is moved to _below_ the platform boundary.

So now, the HAL is responsible for receiving the UART data into a DMA ring, and the BSP is responsible for decoding it, instead simply presenting the application with a queue of events saying "hey, this is what's happened since you last checked." And the application can go "neat, I need these things to change" and the BSP will properly convert those outgoing commands to the hardware-specific operations that need to be performed.

![A pad press on main arrives in the application as raw PIC bytes. On next it passes through the HAL and BSP and reaches the application as a pad event, and pad colours travel back the same way.](../../../assets/blog/what-the-deluge-stands-on/pad-press.svg)

_One pad press and its LED response. On `main`, the app handles raw bytes. On `next`, it receives a pad event and sends pad colours, and nothing above the gold line knows the PIC exists._

## This is not a Rust rewrite

Some may see this as the beginning of a full-stack Rust rewrite. While that may occur in the long term, it's orthogonal to the problem this is solving. Instead, it's simply that Rust is best solution to this particularly problem, due to both its language architecture and Embassy (which will be discussed in a subsequent blog post).

The Deluge application itself is, and remains, C++ due to the advantages that gives us and our limited time.

## The contract: libdeluge

So how is this possible?

Both Rust and C++ (as well as many other languages) interface very well with C. Because of this, we can use C as a way to "translate" between the two layers, creating a contract that both must follow in order to work properly. In programmer terms, this is called an "ABI boundary".

There are now a number of C headers that act as that contract, and even nicer is that using Rust macros, we can enforce the type safety of this boundary, so that mistakes can't catch us out. (You can still make mistakes relating to execution context and threading, but those are much more subtle issues).

<!--
- 38 headers, about 4,900 lines, and the only way through.
- Only plain C crosses, the header is the source of truth, and the app calls down but never includes platform code.
- Each function's docs say where it may be called from: [audio], [task] or [isr].
- The build enforces it: a signature mismatch doesn't compile, and the boundary allowlist on next is empty.
-->

![An example of the contract check: bindgen turns the C header into a Rust declaration, abi_pin compares it with the Rust export, and a wrong type stops the build.](../../../assets/blog/what-the-deluge-stands-on/contract-check.svg)

_How the contract catches a mistake. The `u32` is a made-up error: the real export takes a `u16`._

## In-place replacement

So how does one go about actually doing this?

![Timeline from June to October 2026: new parts are added above the line and old parts are removed below it, ending with the legacy BSP retired on September 29.](../../../assets/blog/what-the-deluge-stands-on/timeline.svg)

_Four months from the first boundary headers to the old BSP being retired, with additions above the line and removals below it._

The first step is to look at the already existing boundary, and begin routing both the Renesas C platform code and the C++ application calls through new C headers.

Following convention of the way platform code is typically provided as a library, I decided to call this platform layer "libdeluge".

Instead of one day just flipping the switch over to Rust, this was instead a multi-week process of migrating the Renesas functions into a new C/C++ static library behind the libdeluge interface, so that there was a proper record and understanding of all the ABI functions we needed to fill.

Once that was done, there were a few more weeks of untangling the ABI to have (mostly) unidirectional calling conventions, and mapping out how to best wire in both the Rust BSP and the other major benefit of doing all this: a new hosted (x86/A64) platform layer.

For a long time all three existed independently, until more and more pieces moved below the ABI boundary (such as the filesystem access and task scheduling), and the original Renesas code was no longer needed.

![Three stages of the swap. First, the Renesas code moved behind libdeluge. Then three implementations of libdeluge ran side by side: the Renesas C code and the Rust BSP on the Deluge, and a C host on a computer. Finally only the Rust BSP remained, built for both the Deluge and a computer.](../../../assets/blog/what-the-deluge-stands-on/three-stages.svg)

_The app and the contract stayed put the whole time. Only what sat underneath libdeluge changed._

<!--
- No flag day: drivers moved behind the contract one at a time, and the firmware kept working throughout.
- New parts came in first, and the old ones were deleted only once nothing needed them.
- Mention the long stretch where both existed, if you have a good story from it.
-->

## Why not keep what was there?

The main reason behind migrating from the platform-specific Renesas code to Rust/Embassy is that Embassy gives us a lot of extra stuff "for free" if we implement the HAL and BSP correctly. Things like USB support for both host and device, interrupt-based task scheduling, etc. Embassy then sits underneath that boundary, alongside the HAL/BSP, allowing us to move even more down into Rust. Again, this will be discussed more in the blog post on tasks and scheduling.

## What does this actually mean for us?

In brief, it allows us to run the Deluge's application code almost anywhere, including on our own computers. Instead of having to rely on slow architectural emulators like QEMU or Unicorn, doing this meant having a hosted platform implementation very early on. This speeds up testing _considerably_, as things like rendering songs and checking hashes can happen in seconds instead of minutes.

Once the Rust/Embassy side of things was introduced and things were migrated onto it, Embassy _itself_ offers a hosted build and executor, meaning the C hosted libdeluge implementation was quickly retired and stopped being a maintenance burden whenever the ABI changed.

Because of this, we now have `./dbt sim`, which is the Deluge, running natively on your computer for simulation, and much more complex testing harnesses for integration testing.

<!--
- The simulator: the same app and BSP run on a computer through dbt sim, which sets up post 2 on testing.
- Better crash information: faults are reported on all vectors.
- Memory: Clang with cross-language LTO shrank the image. Use a fresh measurement here.
-->

![The same C++ application and the same Rust BSP run on the Deluge and in the simulator. Only the endpoints under the BSP change, from real parts to their desktop stand-ins.](../../../assets/blog/what-the-deluge-stands-on/two-worlds.svg)

_Everything above the fork is shared between the Deluge and the simulator. Only the endpoints change._

## What's left?

This was just step one, the beginning so that a rigorous and repeatable testing harness could be made.

Our next post will take a look at exactly that, detailing how work is being done while ensuring that the Deluge's behavior doesn't change unexpectedly.

<!--
- The boundary is enforced at build time, but it's not yet the hard wall the plan calls for.
- Some [audio] calls still allocate or lock. The tags record those hazards, and they aren't fixed yet.
- Supervisor and MMU isolation come last. Close by pointing to post 2: how do you change all this without changing the sound?
-->
