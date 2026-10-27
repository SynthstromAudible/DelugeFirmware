---
title: "Built Together: Community and the Deluge Firmware"
date: 2026-10-26
authors:
  - Mark Adams
---

The Deluge is a strange and wonderful instrument. It’s a sequencer, a sampler, a synth, a groovebox, or a looper, or a drum machine, or a sequencer, or whatever you want it to be, and it fits in a bag.
The firmware that runs it exists because a lot of people care about it, and that was true long before any of us were using AI tools to write code.

I want to talk about a couple things: how we’ve been using AI while working on the firmware, and why it hasn’t changed the need for community

## We love contributions

I've seen a handful of comments around that we're letting our egos drive the deluge development and refusing to use AI
or accept AI PRs. I want to reassure everyone that couldn't be further from the truth - we are all embracing AI as much as it can
help the firmware. My day job is _in_ AI research. We're not luddites or trying to stick our head in the sand and pretend it'll go away (as much as I wish it could).

Nor are we refusing PRs on principle - 80+ people have directly contributed to the deluge code. Many of them have used LLMs and have
little to no development experience. Our guiding principle is that you should be able to articulate why a change is a good
idea in your own words and not use an LLM to communicate with us. This isn't meant to be a judgement on anyone. We're not expecting
people to have performance worked out from the get go, or a perfect UI. We are expecting the ability to talk about the change,
discuss whether it's a good idea, weigh the pros and cons, and work with us to get it merged into the firmware.

#### None of my favourite features are things that I've coded

It's a bit like asking me to pick my favourite student. Of course I have one but I'll never admit it. That said some of
the ones I'd like to highlight as we get closer to the 1.3 release are:

- The new generative arpeggiator courtesy of Raúl Muñoz. I use this all the time, it's the background music of my life
- The completely overhauled UI started by Nikodemus Siivola, continued by Leonid Burygin, featuring a new delugey small
  font design from Casey Tucker with input from Reza Iman
- Tempo automation from Michael Mackus. I did the backend work on this forever ago but he finally did all the hard UI
  work to make it usable :)
- The chord keyboard from Madeline Scyphers
- This website from mgabor (real name unknown but hopefully he'll find this and I'll update it)

I'm excited to see what great ideas people have and hope that the lowered barrier to development will encourage
more people to help with development efforts. We expect people to communicate with us as humans but using an LLM
to write the code is fine, as long as you can accept that there's going to be some back and forth before merging anything.
Which brings us to the second point of discussion

#### AI is a really _really_ good software development tool

As with most of the software industry we've been adopting LLMs in our workflow. The latest generation is really freaking good.
It's hit a point where getting an AI to generate code doesn't feel like a compromise anymore. It needs guidance on architecture,
and it needs to be told what to do, but it can _write code_ as well as any of us can. And there's things it does better than us!

- Building test infrastructure. We've adopted Graham Wheeler's [deluge emulator](https://github.com/SynthstromAudible/delugemu) and are trying to get it as accurate as possible. None of us are experts on QEMU and this has been
  something we wanted for a long time. Opus has made that possible without needing to find a real expert. Giving it my debug deluge and having it spend nights getting the emulator to match it as closely
  as possible has been invaluable.
- Profiling and performance work. Much of the recent speed-up work would have been extremely tedious. Optimizing code for cache usage
  is a painful process of trial and error and benchmarking. The LLM can do that for 12 hours straight until it hits something good,
  I get bored way before that.
- Trying different implementations. Getting USB audio to work without a performance penalty was a journey. The LLM was
  willing to rewrite it a dozen different ways with different tradeoffs until I got something worthwhile. If I was a
  project manager getting a human engineer to do that I'd have been shot.
- Reviewing code.The Deluge is a huge and sprawling system of code, with lots of things that change because of weird edge cases,
  and AI can help us catch that before we merge. It doesn't get distracted or make assumptions in the same way we do and
  that mechanical reading can be very helpful to find errors.
- Hairy refactoring. We can have the AI write out a pile of test cases for the current code, then write new code to match
  those tests millions of times faster than we could do it by hand.

But ultimately AI is not human, and while it can write correct code, someone needs to figure out if it wrote the _right_
code.

#### AI is not human

A language model has no stake in whether your set goes well. It doesn't know the consequences of an update changing
your sounds right before a gig. It doesn't know how bad it feels to have a 15 minute set and spend the first 5 troubleshooting.

I gig the deluge at least once a month and go to jams with it almost every week. I am very invested in keeping it rock solid so I'm never worried about it crashing.
I've been gigging 1.3 for a year and I have never had it crash, but as we all know
tons of people _do_ have crashes on our nightly and beta firmware. How is that possible?

I only have my own hardware and my own habits. You have:

- Songs that are far bigger and stranger than anything I’d make
- SD cards from every manufacturer in every state of health
- Controllers, USB devices, and modular rigs beyond my imagination
- Workflows I would never think to try (shout out to everyone turning the tempo up to FAST)

Every time someone loads a community build and says “this sounds different” or “this crashed when I did that,” the firmware gets better in ways we could not have achieved alone.
Bug reports with steps to reproduce are one of the most valuable things anyone can contribute

## How we work together

We need feedback from real users to improve. We want to know how people are using it!

First off test the builds when you can and let us know what you think. Nightly, beta, and release-candidate builds exist so that real use can find the problems before
people start gigging them.
Something not happening the way you'd expect can be a bug! We want things to be as intuitive as they can be.
Even if you can't say why something feels wrong let us know, “The filter feels different” is a perfectly good bug report.
Suggestions and ideas are welcome - the best way to reach us is the dev channels on [discord](https://discord.gg/s2MnkFqZgj)

We need more help than just code! Helping with documentation is wonderful, the web manual is an evolving work and
it'd be great to get videos and demos to go along with it.

#### On being open about AI

We ask you to write the PR descriptions yourself because we want PRs from humans, we don't want to be spammed by people's AI agents.
It's our filter to ensure that a real person (ideally a musician) has sat down and thought about whether this change makes the project better. It's fine to have
AI write the code, but we need that human judgement to make sure it's the right code.

Everything that lands is reviewed by a person who understands it and is accountable for it. If you have a great idea but the code's not quite there
then we can work on it - we'll give feedback on where we think the problems are and leave it to you to fix them. If you use an AI that's fine, we
just ask that you communicate with us by hand and respect the humans behind the work.

That said everything that gets merged is human reviewed. We make sure changes make sense, follow the UI guidelines, and
feel right on the deluge. This is really a "we" - it's the feedback from the army of beta testers and brave nightly build
users that let us get things right.

If you have concerns or opinions about AI in this project please share those too. I miss the pre AI days as much as I'm
sure many of you do.

### The best firmware is a shared project

The Deluge community has always been the heart of this instrument. It’s the reason it does so much more than it did when the hardware first shipped, and
it's the reason it continues to grow more than 10 years from it's announcement.

The tools make us faster but the community makes us right.

Mark Adams

Co signed by Sean Ditny and Katherine Whitlock - your deluge community maintainers
