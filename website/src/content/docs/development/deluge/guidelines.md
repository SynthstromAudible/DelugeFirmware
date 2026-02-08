---
title: Guidelines for Contributions
---

Everybody is invited to contribute to this repository as further outlined below:

- All contributions are handled with the [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls)
  mechanic in GitHub. If you start working on something please submit a Draft PR so everyone is informed (see below).
- Contributions can be anything that improves the project while fulfilling the requirements outlined below.
- As an inspiration here is a small and incomplete list of improvement areas: documentation, commenting complex code
  mechanics, refactoring (make code easier to read/improve clarity/make execution more efficient/improve
  maintainability/ease modification/improve structure), feature improvements, new features, easier accessibility,
  toolchain improvements, unit tests and many, many more.
- For information on the decision making process for accepting Pull requests please see below and also see
  the [Governance](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/GOVERNANCE.md) documentation.
- To keep the list manageable all Pull requests without author activity for longer than two weeks will be labelled "
  stale" and will be closed after another week. The author is welcome to submit a new PR for the same work at any time.

## Requirements

The following requirements must be fulfilled for a Pull request to be mergable to the `community` branch:

<details><summary>General</summary>

- The contribution must be meaningful to the project. There must be a clear and articulate link between the change and
  an improvement for the developers, users or overall community.
- The Pull request must have a clear scope outlined in the description, please don't commit changes unrelated to the
  scope.
- Refactoring files changed for or related to the Pull request are encouraged. This includes improving code structure
  and file organisation.
- The description of the Pull request must also contain information on what functional areas have been touched and
  should be tested, ideally including a small test manual
- Appropriate branch name, if possible following standard conventions like git flow (e.g.
  feature/shiny_new_feature_name).
- No small Pull requests exclusively fixing single insignificant typos in code comments, one-off formatting mistakes or
  whitespace. Aggregate Pull requests fixing bigger areas can be accepted. (note: this is just for non-code fixes, fixing small bugs is encouraged!)
- Automated and low effort Pull requests will not be will not be tolerated or accepted (
  see [Hacktoberfest](https://blog.domenic.me/hacktoberfest/) for an example on why this rule is necessary).

</details>
<details><summary>Code specific</summary>

- All project files, especially source files need to have a compatible license with the project.
  See [LICENSE](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/LICENSE).
- There is no written standard on code guidelines yet but please make your code match the existing style as much as possible.
- Exception: the old code uses GOTOs and single returns heavily - new C++ code should favour other flow control methods
  and early returns instead, the old code is a result of the project's roots in C.
- Clang Format runs on CI following the config in .clang-format. As there are slight differences between versions, we
  provide a command "./dbt format" to run the CI version exactly, which should gaurantee that your PR passes the checks
- All changes to the firmware have to be tested on a best effort basis to make sure they work as expected and don't
  break any existing functionality before submitting as ready to merge. This does not apply to Draft Pull requests.
- All changes need to be compatible with all available hardware variants, this currently includes OLED and 7-Segment.
- All changes need to be compatible with the currently official toolchain as described in [Tools](/development/deluge/tools).
- Acceptance of the continuous integration (CI) system is also required. It will automatically build open pull requests
  and check for compilation, formatting and in the future possibly unit testing.

</details>
<details><summary>Application specific</summary>

- Pull requests that change how users can interact with the device or massively alter system performance (> 3% permanent
  cycle load) require either:
  - A runtime configuration setting that allows users to enable or disable the feature/change in behavior. See
    documentation on adding optional feature settings (Pull request #56).
  - Or if a runtime setting is not possible, a preprocessor switch that allows creating firmware without the change.
- Changes that massively increase image size (> 5% of total memory) also require a preprocessor switch starting with "
  FEATURE\_" so they can be enabled or disabled.
- If the Pull request requires changes in structure of user files (e.g. project/synth or other xml files) or flash
  configuration:
  - It must ensure that files or flash configuration created with the official firmware or previous community releases
    stay compatible or are automatically upgraded (upward compatibility).
  - If possible files and flash configuration created with the changes of the Pull request can be used with older
    firmwares not having the change (downward compatibility). Older firmwares must not break if a newer configuration
    was stored on the device.
- If the Pull request changes end user behavoir or introduces new features, a new entry in
  the [CommunityFeatures.md](/features/community_features) file needs to be created in the preexisting style describing the
  feature and its options as a small manual to users. This includes all runtime and compile time flags which shall be
  named in respective sections.

</details>
<details><summary>UI Changes</summary>

Changes to the existing UI should follow the following process:

- Ensure they meet the guidelines set out in [UX Principles](/development/deluge/ux_principles)
- Place the change in the community feature menu for one beta cycle
- Following a full beta cycle and user feedback, open a poll on the feature
- At the end of the beta cycle there will be a community meeting on Discord to discuss changes
- Given maintainer approval and positive feedback, the feature can be moved from the community menu to the default UI.
</details>

## Workflow

Please follow the following steps for every pull request to ensure every contribution can be handled as frictionless and
transparent as possible:

1. Create a [Draft Pull request](https://github.blog/2019-02-14-introducing-draft-pull-requests/) including a
   description on what will be changed and what impact you expect. The title can start with "[Draft] " during
   development.
2. Work on the Pull request
3. Before a Pull request can be considered ready, all upstream changes from the `develop` branch need to be merged into
   it. It is the duty of everyone to help make merging into `develop` as painless as possible so please try to align if
   you see that your Pull requests works in a similar area as another one.
4. Once the Pull request is ready, fulfills all requirements outlined above, and is up to date with the `develop`
   branch, it can be converted from Draft and marked as ready for review.
5. Having multiple reviews for every Pull request would be nice. Reviews from community members not mentioned in
   the [CODEOWNERS](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/CODEOWNERS) file should be taken seriously and used as an important source of feedback, but have
   no decisional power on what gets merged into the `develop` branch.
6. At least one member of the [CODEOWNERS](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/CODEOWNERS) file needs to review every pull request while also considering
   community reviews in their decision.
   - CODEOWNERS can decline merging a Pull request if it does not fulfill the requirements outlined above. They need to
     give clear feedback on which requirements have not been met and also provide an opportunity to improve the Pull
     request to meet the requirements within reasonable boundaries (e.g. there are limits on how much work can be
     expected from a CODEOWNER for a specific Pull request).
   - If one or more CODEOWNERS are sure the requirements have been met, they will merge the change into the `develop`
     branch.
   - For more information about governance and handling of decisional matters please take a look at
     the [Governance](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/GOVERNANCE.md) document.

In addition to this workflow it is not a requirement but would be nice if developers could help the maintenance of the
project by:

- Doing housekeeping of the Pull request including processing community feedback.
- Tagging the pull request appropriately where possible.
- Trying to keep some sense of ownership over the touched areas of the codebase and provide support in case of problems,
  questions or future developments.
