# How to contribute to DelugeFirmware/community

Above all, thank you for your interest in both the Synthstrom Deluge and being a contributor!

Please see also, the [Code of Conduct](CODE_OF_CONDUCT.md) document.

## Bug reports

* Please submit an issue highlighting as much *relevant* context about the bug as possible

* Quality of information (rather than quantity, necessarily) is key! See [How to Report Bugs Effectively](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html) by Simon Tatham for more on this topic.

* Do let us know whether the bug was found on an OLED or 7seg Deluge as this may be important.

* Where the bug exists in build scripts, e2, or other development, building, and debugging processes, please provide excerpts of the relevant logs if you suspect they might be helpful.

* Bugs in related projects should be submitted to the appropriate issue tracker:
    * DelugeProbe: [litui/delugeprobe](https://github.com/litui/delugeprobe)
    * DBT-Toolchain: [litui/dbt-toolchain](https://github.com/litui/dbt-toolchain)

## Pull Requests (PR)

* Work-in-progress should be submitted as either a self-assigned issue, a *Draft PR*, or both (ideally, linked to each other). Draft PRs will be considered the *official* way work-in-progress is tracked.

* Try to name your branch appropriately and keep the scope of changes clear (eg: if working on a specific issue, try not to incorporate other unrelated changes or fixes into the same PR)

* As the *community* branch gets updated, please keep your PR's branch updated so it can be easily merged

* As formatting standards are developed, please ensure you are formatting your code accordingly. clang formatting has been included in the GitHub Actions to help with this.

### New features

* If you're looking for ideas for a new feature, please check out the upvoted posts in the [Deluge Suggestions area](https://forums.synthstrom.com/categories/deluge-suggestions) on the [Synthstrom Forum](https://forums.synthstrom.com).

* If you have an idea for a new feature, please check existing [Draft PRs](https://github.com/SynthstromAudible/DelugeFirmware/pulls?q=is%3Apr+is%3Aopen+is%3Adraft) to see if others are working on something similar already.
    * Even if someone else is doing something similar, there may still be opportunity to contribute. Comment on the draft PR with your questions and suggestions.

* Issues and Draft PRs are *not* the place for feature requests by non-developers.

### Low-effort Pull Requests

* Pull requests exclusively fixing insignificant typos in code comments, one-off formatting mistakes, and whitespace will be rejected (though these may be collected together in a bulk fix for a later date).

* Automated PR submissions by clout/karma-seeking accounts (see also: [Hacktoberfest](https://blog.domenic.me/hacktoberfest/)) will not be tolerated or accepted.

## Documentation

* Documentation standards TBD