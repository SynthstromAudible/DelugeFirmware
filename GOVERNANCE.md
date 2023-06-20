# Goal

Goal of the DelugeFirmware community is to produce high quality firmware application releases for users of the Deluge hardware with improvements and new features coming from our community of contributors. To be as transparent and inclusive as possible [contribution guidelines](CONTRIBUTING.md) have been defined with the intent to help reach this goal. Please also see the [Code of Conduct](CODE_OF_CONDUCT.md).


# Development strategy

In order to give everyone in the project clear expecatations and transparency on what is going on all development is organized in the form of Pull requests. This ensures visibility on the ongoing activities and prevents intransparent internal changes from disrupting community work on Pull request. 

There is no official roamdap or project activities, instead all contirbutors are welcome to participate in the discussion and planning process as outlined in the [contribution guidelines](CONTRIBUTING.md). Members of the community can build informal working groups to achieve greater goals and work on them in the form of collaborative Pull requests.


# Release strategy

There are releases coming from the ```develop``` branch as beta as well as the ```community``` branch as official releases. 

For ```develop``` or beta releases:
* All incoming changes come from merging Pull requests from the community.
* For every merged Pull request a new beta firmware will be generated and published on GitHub Releases tagged as ```beta``` to allow users to test them.
* The project management team will decide on the right time to pick a promising beta firmware to be a release candidate.

For ```community``` or official releases the following applies:
* Before a firmware can be released as official community version it must have been released and published in the Github Releases as release candidate (```rc```) for at least two weeks, giving users and developers the chance to thoroughly test them and give feedback.
* Only if the project management team has the feeling that the firmware is stable and delivers a better experience than the previous firmware it is released as an official new ```community``` release in the Github Releases. 






