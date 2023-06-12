# Governance of the Deluge Community Firmware Project

## Direction and Strategy

At present, Ian and Rohan of Synthstrom Audible are taking a "hands-off" approach to the Community repository. While they may offer comments and suggestions from time to time on its direction, [@ian-jorgensen has indicated](https://github.com/SynthstromAudible/DelugeFirmware/discussions/13#discussioncomment-6147993) that Synthstrom would ideally like the community to work out what the `community` fork's direction and strategy will be:

> I think it's going to take a wee while for this to become clearer. Things are moving really fast and I guess key thing is that developments that use key UI are thoughtfully considered/voted on before being implemented. The community firmware does need to be at least pretty stable - compared to the experimental releases and its going to do peoples heads in if UI keeps changing for the community release.
> We (Synthstrom) are trying to stay a little hands off with this tbh and let the community work it out. We've got people helping with PR's and general support, but I felt it wouldn't be that fair for us to also have people setting up strategy etc, at that point its more or less us and no point to this all, haha. But I guess it might pay for someone to take a bit more of a 'strategy' role and create some documents which outline all the avail UI - slotting in possible features into that and making sure nothing is given up too hastily.
> Create communication methods for the community to feedback directly to the dev's. There is lots that can be done in that regards, just needs some peeps with time and desire to manage that sort of planning.
> Also, the creator has to be wiling to give their code back to the community. Lots of things need to come together and it might take a little while for things to get clearer.

Via @jamiefaye in Discord:

> There are two levels of [governance] here. What goes into the Community branch and what goes into the Official branch. The Official level is up to Synthstrom. My goal as a contributor will be to make stuff that eventually goes into Official, with Community as a waypoint.

Via @litui in [Discord](https://discord.com/channels/608916579421257728/1107026299945496577/1117474217475199016):

> I think at some point community will need to level-set on vision for the Deluge. Will it aim for more comprehensive complexity like a DAW or will it aim to shorten the path to production?  Are these the same or different goals? etc etc.

> More features in a build doesn't necessarily make music-making easier. Sometimes simplicity is best.

### Stability First and Foremost?

Via @phalk in [Discord](https://discord.com/channels/608916579421257728/1107026299945496577/1117505878208286771):

> My opinion on community firmware merge and release strategy is that we should have at least one branch that works similarly to how the official firmware has worked until now. Only stable changes that don't break compatibility, work for both OLED and 7seg and don't break the classic UI should be merged, so people can rely on a stable interface and crash free software for live performances etc.

> Of course, there can also be more experimental branches, but for that it would make sense to have a dedicated branch manager with a vision and a set of goals in my opinion.

Via @litui in [Discord](https://discord.com/channels/608916579421257728/1107026299945496577/1117469438426484786):

> Personally I'd err more on the side of having the community repo be a place for "the best of" kind of showing of what the community has to offer, in as stable a package as possible.

### Accessibility?

Should *Accessibility* be a core goal or a goal for an alternative firmware? Via @litui in [Discord](https://discord.com/channels/608916579421257728/1117475009900847174/1117475987714740406):

> I'd also like to see an accessibility-friendly firmware. Deluge as-is relies too heavily on [visuals and] non-obvious shift-modes for a lot of blind and otherwise disabled folks.

## PR Review and Merges

PRs are currently open to review by the general community and approval/merger by a small team of folks (@jamiefaye and @litui) enlisted by Synthstrom to support the Deluge Open Source project.

Via @jamiefaye in Discord:

> Right now, if something does not sound wrong and builds OK on Windows/e2studio,  it will probably go in.
