# How to contribute to DelugeFirmware/community

Above all, thank you for your interest in both the Synthstrom Deluge and being a contributor!

Please see also, the [Code of Conduct](CODE_OF_CONDUCT.md) document.


# Bug reports

Even if you are not a software developer you can contribute immensely by informing us about bugs and other problems you notice during usage or testing of the firmwares created from this repository and related projects:

* Please submit an [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) highlighting as much *relevant* context about the bug as possible.
* Quality of information (rather than quantity, necessarily) is key! See [How to Report Bugs Effectively](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html) by Simon Tatham for more on this topic.
* Do let us know whether the bug was found on an OLED or 7seg Deluge as this may be important as well as the source and version of firmware (branch/etc).
* Where the bug exists in build scripts, e2, or other development, building, and debugging processes, please provide excerpts of the relevant logs if you suspect they might be helpful.
* Bugs in related projects should be submitted to the appropriate issue tracker:
    * DelugeProbe: [litui/delugeprobe](https://github.com/litui/delugeprobe).
    * DBT-Toolchain: [litui/dbt-toolchain](https://github.com/litui/dbt-toolchain).


# Ideas, feature requests, suggestions and discussions

Please use the [Discussions](https://github.com/SynthstromAudible/DelugeFirmware/discussions) feature in our repository which allows both creating discussion, ideas, polls and more as well as voting on existing topics. 

* Please search first if there is alreay a (draft) [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls) or existing Discussion item related to your idea and start your discussion from there if possible.
* Reading, voting and commenting on discussion items and Pull requests will help a lot to improve the direction of the project.
* Please don't use the repository [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) tracker only for bug reports.
* For further inspiration and discussion there is also the [Deluge Suggestions area](https://forums.synthstrom.com/categories/deluge-suggestions) and [Open Source area](https://forums.synthstrom.com/categories/open-source-) on the [Synthstrom Forum](https://forums.synthstrom.com).
* For more in-depth technical discussion you are welcome to join the #dev-chat channel in the [Deluge User Discord](https://discord.com/invite/gWaJEh6pPP).


# Code review and testing

Pull requests that are no longer marked as Draft and are open for merging should be ready to test and any help is appreciated. 

* This includes both building and testing the firmware as well as reading and reviewing the source code.
* Please use the review and discussion funcationality provided within GitHub.
* Code reviews are intended to increase quality and should not go into nitpicking, please remember the [Code of Conduct](CODE_OF_CONDUCT.md).
* Final desicions about merging of Pull requests is up to the code owners, see below and in [Governance](GOVERNANCE.md).


# Repository contributions

Everybody is invited to contribute to this repository as further outlined below:

* All contributions are handled with the Pull request mechanic in GitHub. If you start working on something please submit a Draft PR so everyone is informed (see below).
* Contributions can be anything that improves the project and fulfills the requirements outlined below.
* As an inspiration here is a small and incomplete list of improvement areas: documentation, code commentation, refactoring to: make code easier to read/improve clarity/be more efficient/improve maintainability/ease modification, feature improvements, new features, easier accessibility, toolchain improvements, unit tests and many many more.
* For information on the decision making process for accepting Pull requests please also see the [Governance](GOVERNANCE.md) documentation.


## Requirements

@TODO: General comments

@TODO: Finish and convert to uniform wording
* Appropriate branch name (try to follow the git flow terminology where possible)
* Clearly scoped change / pull request
* There is no written standard on formatting and coding guidelines yet so please try to make your code look and feel like the rest of the repository and respect the styling decisions taken therein (for example brace position). clang formatting has been included in the GitHub Actions and should help with this.
* Pull requests exclusively fixing insignificant typos in code comments, one-off formatting mistakes, and whitespace will be rejected (though these may be collected together in a bulk fix for a later date).
* Automated PR submissions by clout/karma-seeking accounts (see also: [Hacktoberfest](https://blog.domenic.me/hacktoberfest/)) will not be tolerated or accepted.
* Compatible with all HW variants (OLED and 7SEG)
* Changes that change user interaction or massively alter system performance require (> 5% cycle load)
    * A runtime configuration setting that allows to enable or disable the feature/change in behavior
    * If a runtime setting is not possible a preprocessor switch must be implemented
* Changes that massively alter image size (> +5% of memory) also require a preprocessor switch so they can be enabled or disabled
* Code has to be tested and does not break any existing functionality
* At least one approval from one member of the CODEOWNERS
* Whenever possible two approved reviews in total


## Workflow

@TODO: General comments

@TODO: Finish and convert to uniform wording
* Create a Draft PR including a description on what will be changed
* Work on the PR and collaborate with the community
* Upstream changes from the community branch need to be merged into all pull requests before they can be reviewed. It is everyones duty to help make merging into community as painless as possible so please try to align if you see that your pull requests works in a similar area as another one.
* Update the PR once it is ready to be included and make sure all Requirements are met
* If required update the PR to conform with the requirements and get approval from the CODEOWNERS
* If all is well 
