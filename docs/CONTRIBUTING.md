# How to contribute to DelugeFirmware

Above all, thank you for your interest in both the Synthstrom Deluge and being a contributor!

Please see also, the [Code of Conduct](CODE_OF_CONDUCT.md) document.

## LLM usage

This is ultimately a human project. Having an LLM code a fun feature for your own use is great but that 
does not mean we will include it in our supported firmware. 
While we welcome the use of LLM assistance (and we all use it) you must put in the effort to ensure your code is explainable and maintainable. 
A good measure before submitting is ensuring you can read and understand your code clearly enough to hand write a PR description explaining what it does and why. 
If we feel like we are talking to an LLM we will just close PRs.

As a bare minimum:
* do not use an agent to open PRs or comment on them
* do not use an LLM to write PR descriptions 
* do not open PRs with test plans you have not followed


## Bug reports

Even if you are not a software developer you can contribute immensely by informing us about bugs and other problems you
notice during usage or testing of the firmwares created from this repository and related projects:

* Please submit an [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) highlighting as much *relevant*
  context about the bug as possible.
* Quality of information (rather than quantity, necessarily) is key!
  See [How to Report Bugs Effectively](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html) by Simon Tatham for more
  on this topic.
* Do let us know whether the bug was found on an OLED or 7seg Deluge as this may be important as well as the source and
  version of firmware (branch/etc.).
* Where the bug exists in build scripts, editors, or other development, building, and debugging processes, please
  provide excerpts of the relevant logs if you suspect they might be helpful.
* Bugs in related projects should be submitted to the appropriate issue tracker:
    * DelugeProbe: [litui/delugeprobe](https://github.com/litui/delugeprobe).
    * DBT-Toolchain: [SynthstromAudible/dbt-toolchain](https://github.com/SynthstromAudible/dbt-toolchain/issues).

## Ideas, feature requests, suggestions and discussions

Please use the [Discussions](https://github.com/SynthstromAudible/DelugeFirmware/discussions) feature in our repository
which allows both creating discussion, ideas, polls and more as well as voting on existing topics.

* Please search first if there is already a (
  draft) [Pull request](https://github.com/SynthstromAudible/DelugeFirmware/pulls) or existing Discussion item related
  to your idea and start your discussion from there if possible.
* Reading, voting and commenting on discussion items and Pull requests will help a lot to improve the direction of the
  project.
* Please use the repository [issue](https://github.com/SynthstromAudible/DelugeFirmware/issues) tracker for problem
  reporting.
* For further inspiration and discussion there is also
  the [Deluge Suggestions area](https://forums.synthstrom.com/categories/deluge-suggestions)
  and [Open Source area](https://forums.synthstrom.com/categories/open-source-) on
  the [Synthstrom Forum](https://forums.synthstrom.com).
* For more in-depth technical discussion you are welcome to join the #dev-chat channel in
  the [Deluge User Discord](https://discord.com/invite/gWaJEh6pPP).

## Repository Contributions

Pull requests that are no longer marked as Draft and are open for merging should be ready to test and any help is
appreciated.

* This includes both building and testing the firmware as well as reading and reviewing the source code.
* Please use the review and discussion functionality provided within GitHub.
* Code reviews are intended to increase quality and should not go into nitpicking, please remember
  the [Code of Conduct](CODE_OF_CONDUCT.md).
* Final decisions about merging of Pull requests is up to the code owners, see [Governance](GOVERNANCE.md).
* If you have a contribution to make then review the [Guidelines for Repository Contributions](/docs/dev/guidelines.md). 

## Labels

Labels are grouped by prefix:

* `type:` (pull requests only): `fix`, `feature`, `refactor`, `deps`, `chore`. Issues use GitHub's
  native **Issue Type** (Bug, Feature, Task) instead.
* `area:` which part of the firmware or project is affected, e.g. `area: audio`, `area: midi`,
  `area: toolchain`.
* `status:` where an issue or PR stands, e.g. `status: needs-info`, `status: needs-testing`.
* `impact:` how a bug shows up, e.g. `impact: crash`, `impact: ux-inconsistent`.

`release-blocker` and `beta-blocker` mark items that must be fixed before a release advances.
Maintainers also set the **Priority** and **Effort** issue fields when triaging.

The full list lives in [`.github/labels.yml`](../.github/labels.yml). To add or change a label, edit that
file in a pull request; it is synced to GitHub automatically when merged.

Pull requests are labeled automatically: `area:` labels come from the files you change, and the `type:` label
comes from the box you tick under **Type of change** in the PR template. A bot comment will point out anything
missing from the description; it never blocks a merge.

## Adding a Runtime Feature Setting (Community Feature)

When introducing a new feature or behavior modification that should be toggleable by users, add a runtime feature setting by following these steps:

1. **Declare the Setting**:
   - In `src/project/features.h`, add your feature to `enum class Feature` after the last one (and point the `static_assert` below `kFeatures` at it), and its entry to `kFeatures`: the name `CommunityFeatures.XML` writes it under, the words for its values (`kFeatureToggleWords` for On/Off) and its factory value. The menu's DEVICE row `features/<name>` is made from that entry.
   - If your setting has more states than simple On/Off, give it a neutral `enum class` and its words beside `ShortcutOverlay` in `features.h`.
   - In `src/deluge/model/settings/runtime_feature_settings.h`, add the matching identifier to `enum RuntimeFeatureSettingType` before `MaxElement` (e.g. `ShortcutOverlay`); `runtime_feature_settings.cpp` asserts each identifier equal to its `Feature`.

2. **Add Localization (l10n) Strings**:
   - `src/deluge/gui/l10n/strings.h`: Add a `STRING_FOR_COMMUNITY_FEATURE_...` identifier to `enum class String`.
   - `src/deluge/gui/l10n/english.json`: Add the key and the OLED display name string.
   - Run `./dbt build` (or `python3 src/deluge/gui/l10n/generate.py ...`) to regenerate `g_english.cpp`.

3. **Name the Setting's Row**:
   - In `src/deluge/gui/l10n/setting_names.cpp`, add the row's names and its `Entry` under `SettingGroupId::DEVICE` at `features/<name>`, keeping `kEntries` sorted. A setting with more states names its choices there, in its values' order. `RuntimeFeatureSettings::init()` gives every feature its factory value from `kFeatures`.

4. **Create the Associated Menu Item**:
   - In `src/ui/scenes/menu/nodes/settings.h`, in the "Community features" section, declare the node: `feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_...), project::Feature::YOUR_SETTING)`, with `Kind::Enum` as a third argument for a setting with more states (see `feature_shortcut_overlay`). A setting that must take effect the moment it is written does so in the host's store, `store_feature` in `src/deluge/gui/ui/shell_host/device_settings.cpp` (see sticky shift); one the open menu lays itself out by also takes `with_after_write` (see `feature_horizontal_menus`).
   - Add a pointer to the node at the end of `kRuntimeFeatureChildren`, the list the Community Features menu shows.
   - The menu's specs check every node against upstream `main`'s menu, recorded in `tests/golden/ui/menu_oracle/tables_3.tsv`: the Community Features rows are those whose path starts `settings/11/`, and `settings/11`'s `kids` row counts them. Its rows hold what `main`'s source answers (`tests/golden/README.md`, "Menu reference tables"), never what the scenes' menu renders, except the rows that README lists as accepted differences.
   - A setting `main` also has: write its rows from `main`'s item (`git show origin/main:src/deluge/gui/menu_item/runtime_feature/settings.cpp`) under `settings/11/N`, `N` being the node's index in `kRuntimeFeatureChildren` (the paths count the scenes' menu's children, which D156 leaves two short of `main`'s). Copy a neighbouring toggle's rows (every row whose path is `settings/11/0`), give them the item's own name and title, and raise the `kids` row by one.
   - A setting `main` does not have is an accepted difference from `main`, recorded in four places:
     1. A row of `tests/ui-scripts/parity/DIFFERENCES.md` with the next free number, saying what `main` lacks, its status `accepted` with who accepted it, and `tables_3` `settings/11/N` as its evidence.
     2. That row's number in `tests/golden/README.md`'s list of rows that differ from `main` on purpose ("Menu reference tables").
     3. Port-only rows in `tables_3.tsv`, written as for a setting `main` has but from the scenes' node, with the `kids` row raised by one.
     4. A pin in `sim/scenes_spec/support/known_differences.h`, which names each excused node by identity and their number by count, so a difference that is fixed, or one added without its pin, turns a spec red. Add the node to `port_only_setting` and raise `kPortOnlySettings` by one. The first such setting adds both, in a `@name` group of their own, in the shape of `shortcut_name_is_placeholder` and `kShortcutPlaceholderNameCells`:

       ```cpp
       /// @brief Community Features nodes `main`'s menu does not have: their `tables_3` rows are the port's own, each an
       ///        accepted row of `tests/ui-scripts/parity/DIFFERENCES.md`.
       inline bool port_only_setting(const menu::Node& node) {
       	namespace nodes = deluge::ui::scenes::menu::nodes;
       	return &node == &nodes::feature_your_setting;
       }

       /// @brief How many nodes `port_only_setting` names.
       inline constexpr size_t kPortOnlySettings = 1;
       ```

       and adds this example to `tables_3_spec.cpp`'s `tables_3_shape` group (`known_differences.h` is already included there):

       ```cpp
       	it("pins the community features main's menu does not have, by identity and by count", _ {
       		size_t port_only = 0;
       		for (const menu::Node* node : nodes::kRuntimeFeatureChildren) {
       			port_only += spec::port_only_setting(*node) ? 1 : 0;
       		}
       		expect(port_only).to_equal(spec::kPortOnlySettings);
       	});
       ```
   - Then run `./dbt test ui-scenes-spec`, and `./dbt test --update ui-menu-dump` to regenerate `website/src/data/generated/menu-tree.json`.

5. **Guard the Feature in Code**:
   - Include `model/settings/runtime_feature_settings.h` where needed and check `runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::YourSetting)` or `runtimeFeatureSettings.get(RuntimeFeatureSettingType::YourSetting)`.

## Additional Developer Resources

* Access more developer resources in [Additional Information](/docs/dev/additional_info.md).