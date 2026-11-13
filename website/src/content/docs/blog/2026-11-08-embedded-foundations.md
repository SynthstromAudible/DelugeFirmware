---
title: "Embedded Foundations"
date: 2026-11-08
authors:
  - katherine-whitlock
---

Hi everyone, I'm Katherine Whitlock, one of the core maintainers of the Deluge's Community Firmware alongside Mark and Sean. For the last six to eight months I've been working on a branch internally called `next`, which is planned to be the second major release (i.e. 2.x). As we get closer to releasing it, currently planned for the end of January, I thought it might be worth doing a set of blog posts on _what_ the major changes are and why they're being done. These include, but are not limited to, topics like:

- HALs and BSPs (this post)
- Testing and development strategies for the Deluge
- File reading, writing, and streaming (async vs synchronous I/O and cache coherence)
- Multi-tasking and scheduling for music devices
- Atomic model state and data flow
- Embedded UI architecture
- Soft real-time render loops
- Compile-time composable SIMD DSP components and other optimization strategies
- The Deluge's synth architecture

And many other things!

<!-- excerpt -->

So without further ado, here's the first entry in the series:

<!-- Prompts like this one are HTML comments, so they never appear on the page. -->

## Embedded Foundations

Have you ever looked at a synthesizer or sampler or Eurorack module and wondered, "Wow, there's a lot of stuff going on there. How does it know what I'm doing and how I'm interacting with it?" If so, then you're in luck. If not, then maybe this will give some insight anyway, so that in the future you'll know how this all works.

In modern embedded devices, such as synthesizers, samplers, keyboards, controllers, Eurorack modules, etc., it's very rare that whatever does all the audio processing is written so specifically for a processor that it's manually poking all the memory addresses and registers to communicate with all the buttons, switches, knobs, and screens that may exist.

Instead, this is often left to a dedicated platform layer, allowing some level of abstraction between whatever runtime or application code may exist and the hardware itself. This increases portability and ease of development, while also allowing for better separation of concerns.

For example, many devices and products run on the Arduino framework. This framework is responsible for knowing details of both the CPU and (to some degree) the board, and then presenting an easy-to-use facade over top which allows fairly easy portability between various Arduino boards. It is a relatively simple endeavor to port a program written for an older Uno R3 to a Mega or even an Adafruit Metro.

![One line of Arduino code, digitalWrite(LED_BUILTIN, HIGH), runs unchanged on an Uno R3 and a Mega 2560, which use AVR chips, and on an Adafruit Metro M0 Express, which uses an ARM chip. The framework maps pin 13 to port pin PB5 on the Uno, PB7 on the Mega and PA17 on the Metro.](../../../assets/blog/what-the-deluge-stands-on/arduino-boards.svg)

_The same line of code can be used across multiple boards. The framework knows how pin names map to specific port/pin combos for a given CPU and board._

## How things work today

On the Deluge, this layer originally started out as Renesas' own vendor platform code and drivers (Renesas being the manufacturer of the Deluge's CPU, the RZ/A1L), and became entangled with the application code as it was further developed. This is evident in the way that both the RZ/A1L platform code and the application call into each other, and the way that the application does things such as reading raw bytes from the PIC or interacting with the SD routine.

Essentially, the Deluge is married to its hardware by virtue of this entanglement, and in order to get it running on anything else (such as x86 or ARM64), this had to be remedied.

The solution to this was to start from scratch: an entirely new set of layers, written in Rust for safety, running on top of the Embassy runtime framework, and abstracting away all the platform details and drivers that an application shouldn't need to know to run.

Ultimately this required recreating most of Renesas' drivers, and even writing new ones, that were up to spec and matched what we needed.

![Before and after map. On main, the sequencer, sound engine, UI and project code share one program with the hardware drivers, task scheduler, FatFS filesystem, fault handler and heap cores. On next, the first four stay in the C++ app above libdeluge, and the other five moved below it into the Rust platform.](../../../assets/blog/what-the-deluge-stands-on/what-moved.svg)

_How old drivers map to implementations and crates in the new stack, as well as the libdeluge boundary used to enforce this._

## HALs and BSPs

A "HAL" or "Hardware Abstraction Layer" acts as the first real layer between an application and silicon. It's typically intended for a single CPU model or a CPU family, and has details about which memory locations correspond to different peripherals. In the case of Rust, it also acts as a safety barrier, presenting a safe "owning" API that works in conjunction with Rust's borrow checker to hide the messy unsafe details of poking raw registers and memory locations.

A "BSP" or "Board Support Package" is exactly what it says on the tin: if the HAL abstracts details of a CPU, then the BSP does the same for an entire board. This includes things like drivers for whatever I2C, SPI, and UART devices may be connected to the CPU, and also simplifications for interacting with them. This is also where a lot of aliasing and binding happens, so that instead of saying "turn on the LED on pin 13 in inverted mode", you can instead say "turn on the SYNC LED".

Together, these two layers act as a way to hide details inside them. Previously, if you pressed a button on the Deluge, the application would read raw bytes directly from the UART peripheral, queue up events and handle the appropriate response itself before sending out raw color bytes. Now that behavior has moved _below_ the platform boundary.

So now, the HAL is responsible for receiving the UART data into a DMA ring, and the BSP is responsible for decoding it and simply presenting the application with a queue of events saying "hey, this is what's happened since you last checked." And the application can go "neat, I need these things to change" and the BSP will properly convert those outgoing commands to the hardware-specific operations that need to be performed.

![A pad press on main arrives in the application as raw PIC bytes. On next it passes through the HAL and BSP and reaches the application as a pad event, and pad colors travel back the same way.](../../../assets/blog/what-the-deluge-stands-on/pad-press.svg)

_A single pad press and its LED response. On the current firmware, the app handles raw bytes. On `next`, it receives a pad event and sends pad colors, while the layer below is actually responsible for the hardware-specific translation and messaging._

## This is not a Rust rewrite

Some may see this as the beginning of a full-stack Rust rewrite. While that may occur in the long term, it's orthogonal to the problem this is solving. Instead, it's simply that Rust is the best solution to this particular problem, due to both its language architecture and the Embassy framework (which will be discussed in a subsequent blog post). Rust is very useful for the kind of safety-critical code that _should_ exist below this boundary, such as an allocator and platform-specific cache management.

In addition to this, Embassy gives us a lot of extra stuff "for free" if we implement the HAL and BSP correctly. Things like USB support for both host and device, interrupt-based task scheduling, etc. This is all extremely valuable for a (soft) real-time instrument, and means that we don't have to maintain as much code ourselves.

The Deluge application itself is, and remains, C++ due to the advantages that gives us and our limited time.

## The boundary: libdeluge

Both Rust and C++ (as well as many other languages) interface very well with C. Because of this, we can use C as a way to "translate" between the two layers, creating a contract that both must follow in order to work properly. In programmer terms, this is called an "ABI boundary".

There are now a number of C headers that act as that contract, and, even better, by using Rust macros we can enforce the type safety of this boundary, so that mistakes can't catch us out. (You can still make mistakes relating to execution context and threading, but those are much more subtle issues.)

<!--
- 38 headers, about 4,900 lines, and the only way through.
- Only plain C crosses, the header is the source of truth, and the app calls down but never includes platform code.
- Each function's docs say where it may be called from: [audio], [task] or [isr].
- The build enforces it: a signature mismatch doesn't compile, and the boundary allowlist on next is empty.
-->

![An example of the contract check: bindgen turns the C header into a Rust declaration, abi_pin compares it with the Rust export, and a wrong type stops the build.](../../../assets/blog/what-the-deluge-stands-on/contract-check.svg)

_How this contract catches a mistake: introducing a `u32` where a `u16` is expected causes the Rust binding system to emit a compile-time error rather than silently continue._

## In-place replacement

![Timeline from June to October 2026: new parts are added above the line and old parts are removed below it, ending with the legacy BSP retired on September 29.](../../../assets/blog/what-the-deluge-stands-on/timeline.svg)

_The retirement timeline, over four months._

The first step to migrating all of this was to look at the existing boundary, and begin routing both the Renesas C platform code and the C++ application calls through new C headers.

Following the convention that platform code is typically provided as a library, I decided to call this platform layer "libdeluge".

Rather than just flipping the switch over to Rust one day, this was a multi-week process of migrating the Renesas functions into a new C/C++ static library behind the libdeluge interface, so that there was a proper record and understanding of all the ABI functions we needed to fill.

Once that was done, there were a few more weeks of untangling the ABI to have (mostly) unidirectional calling conventions, and mapping out how to best wire in both the Rust BSP and the other major benefit of doing all this: a new hosted (x86/A64) platform layer.

For a long time, all three existed independently, until more and more pieces moved below the ABI boundary (such as the filesystem access and task scheduling), and the original Renesas code was no longer needed.

![Three stages of the swap. First, the Renesas code moved behind libdeluge. Then three implementations of libdeluge ran side by side: the Renesas C code and the Rust BSP on the Deluge, and a C host on a computer. Finally only the Rust BSP remained, built for both the Deluge and a computer.](../../../assets/blog/what-the-deluge-stands-on/three-stages.svg)

_How things changed beneath libdeluge over the migration._

<!--
- No flag day: drivers moved behind the contract one at a time, and the firmware kept working throughout.
- New parts came in first, and the old ones were deleted only once nothing needed them.
- Mention the long stretch where both existed, if you have a good story from it.
-->

## The end goal

To be brief, doing all this allows us to run the Deluge's application code almost anywhere, including on our own computers. Instead of having to rely on slow architectural emulators like QEMU or Unicorn, we had a hosted platform implementation very early on. This speeds up testing _considerably_, as things like rendering songs and checking hashes can happen in seconds instead of minutes.

Once the Rust/Embassy side was introduced and things were migrated onto it, Embassy _itself_ offers a hosted build and executor, meaning the C hosted libdeluge implementation was quickly retired and stopped being a maintenance burden whenever the ABI changed.

Because of this, we now have `./dbt sim`, which is the Deluge running natively on your computer for simulation, and much more complex testing harnesses for integration testing.

<!--
- The simulator: the same app and BSP run on a computer through dbt sim, which sets up post 2 on testing.
- Better crash information: faults are reported on all vectors.
- Memory: Clang with cross-language LTO shrank the image. Use a fresh measurement here.
-->

![The same C++ application and the same Rust BSP run on the Deluge and in the simulator. Only the endpoints under the BSP change, from real parts to their desktop stand-ins.](../../../assets/blog/what-the-deluge-stands-on/two-worlds.svg)

_How the application is shared and reused between multiple platforms without modification other than new platform library linking._

## What's left?

This was just step one: the groundwork that made a rigorous and repeatable testing harness possible.

Our next post will take a look at exactly that, detailing how work is being done while ensuring that the Deluge's behavior doesn't change unexpectedly.

If you'd like to use the HAL and BSP for your own projects, they're available as part of the [Deluge SDK](https://github.com/FirestormAudio/deluge-sdk), which is currently in Developer Preview. The SDK will get a blog post/announcement when it's fully released in its own right.

<!--
- The boundary is enforced at build time, but it's not yet the hard wall the plan calls for.
- Some [audio] calls still allocate or lock. The tags record those hazards, and they aren't fixed yet.
- Supervisor and MMU isolation come last. Close by pointing to post 2: how do you change all this without changing the sound?
-->
