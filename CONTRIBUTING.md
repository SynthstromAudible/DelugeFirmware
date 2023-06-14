# How to contribute to DelugeFirmware/community

Above all, thank you for your interest in both the Synthstrom Deluge and being a contributor!

Please see also, the [Code of Conduct](CODE_OF_CONDUCT.md) document.

# Bug reports

Even if you are not a software developer you can contribute immensely by informing us about bugs and other problems you notice during usage or testing of the firmwares created from this repository and related projects:

* Please submit an [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) highlighting as much *relevant* context about the bug as possible

* Quality of information (rather than quantity, necessarily) is key! See [How to Report Bugs Effectively](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html) by Simon Tatham for more on this topic.

* Do let us know whether the bug was found on an OLED or 7seg Deluge as this may be important as well as the source and version of firmware (branch/etc.)

* Where the bug exists in build scripts, e2, or other development, building, and debugging processes, please provide excerpts of the relevant logs if you suspect they might be helpful.

* Bugs in related projects should be submitted to the appropriate issue tracker:
    * DelugeProbe: [litui/delugeprobe](https://github.com/litui/delugeprobe)
    * DBT-Toolchain: [litui/dbt-toolchain](https://github.com/litui/dbt-toolchain)

# Ideas, feature requests, suggestions and discussions

Please use the [Discussions](https://github.com/SynthstromAudible/DelugeFirmware/discussions) feature in our repository which allows both creating discussion, ideas, polls and more as well as voting on existing topics. 

* Please search first if there is alreay a (draft) [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls) or existing Discussion item related to your idea and start your discussion from there if possible.
* Reading, voting and commenting on discussion items and Pull requests will help a lot to improve the direction of the project
* Please don't use the repository [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) tracker only for bug reports
* For further inspiration and discussion there is also the [Deluge Suggestions area](https://forums.synthstrom.com/categories/deluge-suggestions) and [Open Source area](https://forums.synthstrom.com/categories/open-source-) on the [Synthstrom Forum](https://forums.synthstrom.com)
* For more in-depth technical discussion you are welcome to join the #dev-chat channel in the [Deluge User Discord](https://discord.com/invite/gWaJEh6pPP)

## Code review and testing

Pull requests that are no longer marked as Draft and are open for merging should be ready to test and any help is appreciated. 
* This includes both building and testing the firmware as well as reading and reviewing the source code
* Please use the review and discussion funcationality provided within GitHub
* Code reviews are intended to increase quality and should not go into nitpicking, please remember the [Code of Conduct](CODE_OF_CONDUCT.md).
* Final desicions about merging of Pull requests is up to the code owners, see below and in [Governance](GOVERNANCE.md)


# @TODO - All of the following

# Repository contributions

Everybody is invited to contribute to the community branch of this repository in various ways outlined below. For information on the decision making process for accepting changes into please also see the [Governance](GOVERNANCE.md) documentation.

## Core improvements and features

Core improvements include everything that makes the overall project better including documentation, code commentation, refactoring for clarity, rea, 




## Requirements
* Compatible with OLED and 7SEG
* Changes that change user interact require either runtime configuration or compiler switch
* Changes that massively alter system performance
* Changes that massively alter image size


## Workflow 



https://github.blog/2017-07-06-introducing-code-owners/ -> At least one person












## Pull Requests (PR)

* Work-in-progress should be submitted as either a self-assigned issue, a *Draft PR*, or both (ideally, linked to each other). Draft PRs will be considered the *official* way work-in-progress is tracked.

* Try to name your branch appropriately and keep the scope of changes clear (eg: if working on a specific issue, try not to incorporate other unrelated changes or fixes into the same PR)

* As the *community* branch gets updated, please keep your PR's branch updated so it can be easily merged

* As formatting standards are developed, please ensure you are formatting your code accordingly. clang formatting has been included in the GitHub Actions to help with this.


### Low-effort Pull Requests

* Pull requests exclusively fixing insignificant typos in code comments, one-off formatting mistakes, and whitespace will be rejected (though these may be collected together in a bulk fix for a later date).

* Automated PR submissions by clout/karma-seeking accounts (see also: [Hacktoberfest](https://blog.domenic.me/hacktoberfest/)) will not be tolerated or accepted.

## Documentation

* Documentation standards TBD



* Types of contributions
    * Bug reports

    * Core improvements
    * Features
    * Documentation
* Workflow
    * Pull requests
        * Draft PR
        * Review
        * 
    * Governance