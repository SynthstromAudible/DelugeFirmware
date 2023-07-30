# How to contribute to DelugeFirmware

Above all, thank you for your interest in both the Synthstrom Deluge and being a contributor!

Please see also, the [Code of Conduct](CODE_OF_CONDUCT.md) document.


# Bug reports

Even if you are not a software developer you can contribute immensely by informing us about bugs and other problems you notice during usage or testing of the firmwares created from this repository and related projects:

* Please submit an [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) highlighting as much *relevant* context about the bug as possible.
* Quality of information (rather than quantity, necessarily) is key! See [How to Report Bugs Effectively](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html) by Simon Tatham for more on this topic.
* Do let us know whether the bug was found on an OLED or 7seg Deluge as this may be important as well as the source and version of firmware (branch/etc.).
* Where the bug exists in build scripts, e2, or other development, building, and debugging processes, please provide excerpts of the relevant logs if you suspect they might be helpful.
* Bugs in related projects should be submitted to the appropriate issue tracker:
    * DelugeProbe: [litui/delugeprobe](https://github.com/litui/delugeprobe).
    * DBT-Toolchain: [litui/dbt-toolchain](https://github.com/litui/dbt-toolchain).


# Ideas, feature requests, suggestions and discussions

Please use the [Discussions](https://github.com/SynthstromAudible/DelugeFirmware/discussions) feature in our repository which allows both creating discussion, ideas, polls and more as well as voting on existing topics. 

* Please search first if there is already a (draft) [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls) or existing Discussion item related to your idea and start your discussion from there if possible.
* Reading, voting and commenting on discussion items and Pull requests will help a lot to improve the direction of the project.
* Please don't use the repository [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) tracker only for bug reports.
* For further inspiration and discussion there is also the [Deluge Suggestions area](https://forums.synthstrom.com/categories/deluge-suggestions) and [Open Source area](https://forums.synthstrom.com/categories/open-source-) on the [Synthstrom Forum](https://forums.synthstrom.com).
* For more in-depth technical discussion you are welcome to join the #dev-chat channel in the [Deluge User Discord](https://discord.com/invite/gWaJEh6pPP).


# Code review and testing

Pull requests that are no longer marked as Draft and are open for merging should be ready to test and any help is appreciated. 

* This includes both building and testing the firmware as well as reading and reviewing the source code.
* Please use the review and discussion functionality provided within GitHub.
* Code reviews are intended to increase quality and should not go into nitpicking, please remember the [Code of Conduct](CODE_OF_CONDUCT.md).
* Final decisions about merging of Pull requests is up to the code owners, see below and in [Governance](GOVERNANCE.md).


# Repository contributions

Everybody is invited to contribute to this repository as further outlined below:

* All contributions are handled with the [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls) mechanic in GitHub. If you start working on something please submit a Draft PR so everyone is informed (see below).
* Contributions can be anything that improves the project while fulfilling the requirements outlined below.
* As an inspiration here is a small and incomplete list of improvement areas: documentation, commenting complex code mechanics, refactoring (make code easier to read/improve clarity/make execution more efficient/improve maintainability/ease modification/improve structure), feature improvements, new features, easier accessibility, toolchain improvements, unit tests and many, many more.
* For information on the decision making process for accepting Pull requests please see below and also see the [Governance](GOVERNANCE.md) documentation.


## Requirements

The following requirements must be fulfilled for a Pull request to be mergable to the `develop` branch:

### General

* The contribution must be meaningful to the project. There must be a clear and articulate link between the change and an improvement for the developers, users or overall community.
* The Pull request must have a clear scope outlined in the description, please don't commit changes unrelated to the scope.
* Refactorings files changed for or related to the Pull request are encouraged. This includes improving code structure and file organisation.
* The description of the Pull request must also contain information on what functional areas have been touched and should be tested, ideally including a small test manual
* Appropriate branch name, if possible following standard conventions like git flow (e.g. feature/shiny_new_feature_name).
* No small Pull requests exclusively fixing single insignificant typos in code comments, one-off formatting mistakes or whitespace. Aggregate Pull requests fixing bigger areas can be accepted.
* Automated and low effort Pull requests will not be will not be tolerated or accepted (see [Hacktoberfest](https://blog.domenic.me/hacktoberfest/) for an example on why this rule is necessary).

### Code specific

* All project files, especially source files need to have a compatible license with the project. See [LICENSE](LICENSE).
* There is no written standard on formatting and coding guidelines yet so the current requirement is to make your code look and feel like the rest of the repository and respect the styling decisions taken therein (for example brace position). Clang format checking has been defined in the `.clang-format` file. It is checked in the GitHub Actions to enforce some guidelines. To check for compliance currently Clang 14 should be used since other versions have different behavior.
* All changes to the firmware have to be tested on a best effort basis to make sure they work as expected and don't break any existing functionality before submitting as ready to merge. This does not apply to Draft Pull requests.
* All changes need to be compatible with all available hardware variants, this currently includes OLED and 7-Segment.
* All changes need to be compatible with the currently official toolchain as described in the [Readme](README.md).
* Acceptance of the CI system is also required. It will automatically build open pull requests and check for compilation, formatting and in the future possibly unit testing

### Application specific

* Pull requests that change how users can interact with the device or massively alter system performance (> 3% permanent cycle load) require either.
    * A runtime configuration setting that allows to enable or disable the feature/change in behavior, see documentation on adding optional feature settings (Pull request #56).
    * Or if a runtime setting is not possible a preprocessor switch that allows creating firmware without the change.
* Changes that massively increase image size (> 5% of total memory) also require a preprocessor switch starting with "FEATURE_" so they can be enabled or disabled.
* If the Pull request requires changes in structure of user files (e.g. project/synth or other xml files) or flash configuration.
    * It must ensure that files or flash configuration created with the official firmware or previous community releases stay compatible or are automatically upgraded (upward compatibility).
    * If possible files and flash configuration created with the changes of the Pull request can be used with older firmwares not having the change (downward compatibility). Older firmwares must not break if a newer configuration was stored on the device.
* If the Pull request changes end user behavoir or introduces new features a new entry in the [CommunityFeatures.md](Documentation/CommunityFeatures.md) file needs to be created in the preexisting style describing the feature and it's options as a small manual to users. This includes all runtime and compile time flags which shall be named in respective sections.

### UI Changes

Changes to the existing UI should follow the following process

* Ensure they meet the guidelines set out in [UX Principles](Documentation/ux_principles.md)
* Place the change in the community feature menu for one beta cycle
* Following a full beta cycle and user feedback, open a poll on the feature
* Given maintainer approval and positive poll results, the feature can be moved from the community menu to the default Ui


## Workflow

Please follow the following steps for every pull request to ensure every contribution can be handled as frictionless and transparent as possible: 

1. Create a [Draft Pull request](https://github.blog/2019-02-14-introducing-draft-pull-requests/) including a description on what will be changed and what impact you expect. The title can start with "[Draft] " during development.
2. Work on the Pull request
3. Before a Pull request can be considered ready all upstream changes from the `develop` branch need to be merged into it. It is the duty of everyone to help make merging into `develop` as painless as possible so please try to align if you see that your Pull requests works in a similar area as another one.
4. Once the Pull request is ready, fulfills all requirements outlined above and is up to date with the `develop` branch it can be converted from Draft and marked as ready for review.
5. Having multiple reviews for every Pull request would be nice. Reviews from community members not mentioned in the [CODEOWNERS](CODEOWNERS) file should be taken serious and used as an important source of feedback but have no decisional power on what gets merged into the `develop` branch.
5. At least one member of the [CODEOWNERS](CODEOWNERS) file needs to review every pull request while also considering community reviews in their decision.
    * CODEOWNERS can decline merging a Pull request if it does not fulfill the requirements outlined above. They need to give clear feedback on which requirements have not been met and also provide opportunity to improve the Pull request to meet the requirements within reasonable boundaries (e.g. there are limits on how much work can be expected from a CODEOWNER for a specific Pull request).
    * If one or more CODEOWNERS are sure the requirements have been met they will merge the change into the `develop` branch.
    * For more information about governance and handling of decisional matters please take a look at the [Governance](GOVERNANCE.md) document.

In addition to this workflow it is not a requirement but would be nice if developers could help the maintenance of the project by:
* Do houskeeping of the Pull request including processing community feedback.
* Tagging the pull request appropriately where possible.
* Trying to keep some sense of ownership over the touched areas of the codebase and support in case of problems, questions or future developments.
