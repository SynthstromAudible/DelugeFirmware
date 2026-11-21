# Loop Song Release Update Checklist

Use this checklist for each new official firmware release that receives the custom
Loop Song feature. Copy the checklist into the update issue or release notes and
record results there; do not mark the shared template as completed.

The presence of this document does not mean a branch includes Loop Song. Hardware
checks apply to the feature build, not to an unchanged upstream baseline.

## Release Record

- Official release URL, tag, and full commit:
- Target feature branch and feature commit(s):
- Toolchain version and build configuration:
- Firmware filename and SHA-256:
- Deluge hardware/display, test date, and tester:
- Results, known limitations, and previous working firmware:

## 1. Preserve The Working Version

- [ ] Save songs and back up the SD card before installing or testing firmware.
- [ ] Keep the previous working firmware binary and its matching source commit.
- [ ] Check `git status`; commit or otherwise preserve local work before switching branches.
- [ ] Confirm the personal fork is `origin` and the official repository is `upstream`.
- [ ] Keep the existing feature branches; do not reset them or force-push an update over them.

## 2. Select The New Base

- [ ] Verify the official stable release tag, excluding nightly and prerelease builds.
- [ ] Fetch the required release tag from `upstream` and record its full commit.
- [ ] Create a new `feature/loop-song-X.Y.Z` branch directly from that tag.
- [ ] Keep release baselines free of firmware changes. A shared checklist-only commit
      is acceptable; the official tag remains the exact upstream reference.
- [ ] Read the target release's build instructions and pinned toolchain requirement.
- [ ] Use a separate build directory so previous working firmware is not overwritten.

Example workflow, replacing the placeholders with verified values:

```sh
git fetch upstream tag RELEASE_TAG
git switch -c feature/loop-song-X.Y.Z RELEASE_TAG
git cherry-pick FEATURE_ONLY_COMMIT
```

Select the actual feature-only commit(s), not a merge commit or unrelated upstream
development commits. Do not merge the entire development branch into a stable release.
If the base does not contain this checklist, bring over its documentation-only commit too.

## 3. Port The Feature

- [ ] Review the existing feature-only diff and cherry-pick or adapt it to the new APIs.
- [ ] Resolve conflicts by reviewing behavior, not by accepting an entire file from either side.
- [ ] Preserve the per-song `loopSong` setting, default Off, and its save/load attribute.
- [ ] Keep `Song > Loop Song` available on OLED and 7SEG, including generated display labels.
- [ ] Preserve final-section repeat counts, bounded section selection, and sparse-section handling.
- [ ] Adapt exclusive-section rules only if the target release supports them.
- [ ] Keep Arranger wrapping at timeline zero without stopping and restarting the transport clock.
- [ ] Preserve recording, resampling, stem-export completion, and manual Stop behavior.
- [ ] Review clip, automation, event scheduling, and clock APIs for changes since the previous base.
- [ ] Review the complete diff against the release tag for unrelated changes.
- [ ] Document any target-version differences and remaining limitations.

## 4. Automated And Build Checks

- [ ] Run the section-selection regression tests with the target toolchain: normal
      advancement, last index, sparse sections, single section, empty sequence, and invalid indices.
- [ ] Run the repository's relevant test targets and playback tests where available.
- [ ] Check that older songs without the setting load with Loop Song Off.
- [ ] Check that saving and reloading preserves both On and Off values.
- [ ] Run formatting, applicable commit hooks, and `git diff --check`.
- [ ] Build the Release firmware with the target release's pinned toolchain.
- [ ] Verify a nonempty flashable binary was produced; record its size and SHA-256.
- [ ] Label the artifact as a custom build based on the verified official tag.
      Do not present it as an official release or silently change upstream version metadata.
- [ ] Record unavailable tests and existing upstream warnings instead of claiming they passed.

The current section-selection checks use compile-time `static_assert` expressions.
Compiling them validates the helper, but does not test the transport, audio engine,
automation, MIDI output, menu, or hardware. Hardware checks below are still required.

## 5. Hardware Regression Checks

Run the applicable tests with Loop Song Off and On. Record the song/setup and any
unsupported cases. Enable the option before starting Grid playback because section
transitions are scheduled ahead of time.

- [ ] Install the custom binary successfully and verify the build/source identity.
- [ ] Find and toggle the setting; save, reload, and power-cycle to check persistence.
- [ ] Grid: play multiple sections with different repetition counts and clip lengths;
      check the final-to-first transition, sparse sections, and the final supported section index.
- [ ] Grid: check a single populated section and an indefinitely repeating section.
- [ ] Arranger: check wrapping, silence before the first clip, gaps between clips,
      muted/soloed tracks, and playback started partway through the arrangement.
- [ ] Check an empty song and empty arrangement for hangs or repeated zero-time events.
- [ ] Listen for missing opening notes, stuck notes, audio clicks, gaps, or overlap at loop boundaries.
- [ ] Check clip and song automation, including tempo automation, over several whole-song loops.
- [ ] Check internal clock, metronome, external MIDI clock, outgoing MIDI clock, and CV/gate if used;
      wrapping must not introduce unintended transport Stop/Start messages or clock discontinuities.
- [ ] Confirm Play stops playback normally; restart and switch between Session and Arranger.
- [ ] Confirm normal clip looping in Clip View for Synth, Kit, MIDI, CV, and Audio clips where applicable.
- [ ] Confirm INFINITE, ONCE, and FILL clip launch modes retain their intended behavior.
- [ ] Confirm Scale mode can be turned off and on in a compatible clip/view and keyboard layout.
- [ ] Confirm Row View, clip launch/mute/solo, recording, and overdubbing retain their normal behavior.
- [ ] Confirm resampling and stem export finish instead of repeating indefinitely.
- [ ] If a regression appears, compare the same setup on the unmodified official release
      and the previous working custom build before attributing it to Loop Song.

## 6. Publish And Keep A Rollback Path

- [ ] Commit feature changes separately from unrelated work; keep this checklist in its own documentation commit.
- [ ] Record the official base tag, feature commits, test results, and remaining limitations.
- [ ] Verify the push destination is the personal fork and publish the new feature branch only when approved.
- [ ] Attach the custom binary and checksum to a clearly labeled custom release when publishing firmware.
- [ ] Retain the previous branch, binary, and SD-card backup until the new version is proven reliable.
- [ ] Keep an upstream pull request in mind as the long-term route to official inclusion.
- [ ] If this shared template changes, propagate only its documentation commit to the other maintained branches.
