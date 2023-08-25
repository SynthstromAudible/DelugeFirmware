# Governance
## Goal

Goal of the DelugeFirmware community is to produce high quality firmware application releases for users of the Deluge hardware with improvements and new features coming from our community of contributors. To be as transparent and inclusive as possible, [contribution guidelines](CONTRIBUTING.md) have been defined with the intent to help reach this goal. Please also see the [Code of Conduct](CODE_OF_CONDUCT.md).


## Development strategy

In order to give everyone in the project clear expectations and transparency on what is going on, all development is organized in the form of Pull requests. This ensures visibility on the ongoing activities and prevents intransparent internal changes from disrupting community work. 

There is no official roamdap or project activities, instead all contributors are welcome to participate in the discussion and planning process as outlined in the [contribution guidelines](CONTRIBUTING.md). Members of the community can build informal working groups to achieve greater goals and work on them in the form of collaborative Pull requests.


## Release strategy

There are `nightly` releases covering all changes of the day as well as stable `community` releases.

`nightly` (beta) releases:
* All incoming changes come from merging Pull requests from the community during the day.
* Every night a new `nightly` firmware will be generated and published on GitHub Releases tagged as `nightly` to allow users to test them.

`community` (stable) releases:
* Community releases will be done in a three months cycle at the end of January, April, July and October
* Before every release a special release candidate branch is created from the community branch at that time. This branch only merges bugfixes and no new features from the community branch. 
* Until release, users and developers can test the release candidate and make sure it is of high quality for the final release on GitHub Releases tagged as `community`.

## What to expect and when to expect it

The project is a community effort of mostly volunteers that define the pace of development with their generous contributions. This means that both the features that are contributed and the timelines cannot be known in advance. The nightly releases should ensure that everyone can test the latest and greatest of what has been achieved as soon as possible (please backup your files before starting any testing). There is also always the possibility to contribute to the project yourself, see the [contribution guidelines](CONTRIBUTING.md).
