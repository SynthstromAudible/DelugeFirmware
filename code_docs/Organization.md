# Community firmware organization
#### To work out
- docs folder, (dev/bug_fixes) cleanup, consistency, how-to-use
- what functionality is in hid vs gui and why?
- why is colour in gui
- why is waveform not in ui or views?

### Naming conventions
#### General observations
- Naming conventions sometimes don't align between the public UI and the code. Song view being called session_view in code is a good example. These differences are also not documented. This makes it difficult for especially people new to the project to find their way
- Some features could have more descriptive names. For example green/blue mode in Grid View are sometimes called launch and edit mode, but are in code and documentation referenced as green and blue. It would be helpful for both users and developers to refer to features by a descriptive name, both in code and documentation
-  Some new menu-items - especially in `community firmware` - don't have descriptive names but are listed as "alternate [x] behavior", which puts the load on the user's memory to remember what they do. It would be helpful to have all menu names be descriptive
-  At the same time, many menu items are longer than the display allows. This is probably not possible to avoid altogether, but navigating a menu with items that fit the display is significantly faster. It could pay off to rework menu items' naming to be both descriptive and short. For example: `stutter quantize` conveys as much information as `stutter rate quantize` while still fitting on the display. *(Note: some longer names from the community features menu will get shorter automatically when they are implemented in their dedicated spot in the regular menu, f.e. `grid view loop layer pads` will become `loop layer pads` when placed in the grid view menu)*
#### Modes vs Views vs Layouts
If there is a learning curve to the Deluge, I feel the biggest hurdle is the lack of distinction between the scope and significance of different views. Currently, everything from the arranger, to the grid, to sequencing, to audio recording, to loading files is called a **view**. Having no structure to place all these fundamentally different features into makes it difficult to build up a good understanding what is happening.

Having a more descriptive, hierarchical naming convention for different kinds of features makes it easier for beginners to feel at ease with the interface, *and* makes it easier for developers to add features in a way that keeps the interface manageable. This could be part of refactoring ui & views, if we decide to do so. A proposal:

1. **Modes** describe mutually exclusive fundamental ways in which the device behaves. Currenly this would only apply to **Song Mode** and **Arranger Mode**. At any time, either one or the other is active - never both. This allows us to rename the Song Mode layouts to **Rows View** and **Grid View**. *This could be implemented without changing anything else.*
2. **Views** exclusively describe ways of **viewing** and **interacting** with a **mode**. Currently this would only apply to **Rows View**, **Grid View** and **Performance View**. *(One could imagine a grid view for arranger mode, where the arrangement plays top to bottom, which would make all views compatible with both modes.)*
3. **Editors:** clip and automation view operate fundamentally different from the more similar aforementioned views. To distinguish them from those views, they are labeled **Clip Editor**, **Clip Automation Editor** and **Arranger Automation Editor**.
4. **Tools:** There is a selection of mostly autonomous tools/features that can be called in different places in the UI, but operate largely the same regardless of where they are called. Think of the browser, waveform editor and audio recorder. These currently aren't called views, but they are documented in the manual under the views they appear in. Instead they should be documented as what they are: a clearly delineated, autonomous tool. *There may be a better name for this.*

The documentation structure would look something like this:
- General UI
  - Browser
  - ...
- Modes
  - Song Mode
  - Arranger Mode
- Views
  - Rows View
  - Grid View
  - Performance View
- Clip Editor *(gets its own main header because of how much happens here, and has a dedicated ui button)*
  - Sequencing
  - Modulation
  - ...
- Tools
  - Automation Editor
  - Waveform Editor
  - Audio Recorder
  - ...


### Code Cleanup
#### Automation View Refactoring
**Proposal:**
When refactoring it would make sense to create a class `AutomationEditor` which just implements the editor itself. Ideally this class would implement a few different layouts for the editing interface, for example the velocity editing layout. When launching the editor, the layout can be set appropriately to the parameter edited.

If necessary, subclasses could be created to implement different behavior. But as all the editors look extremely similar, I feel like having them all in one class would be most efficient.

A separate class `AutomationEditorLaunchpad` would implement the launchpad.

`AutomationEditorLaunchpad` presents different sets of parameters to edit depending on where/how it is launched. Instrument Clip View => [audition pad] + [song] could show Note Automation.

Considerations:
- Note automation like probability could be automated on clip *and* note level. (Which parameters should be automatable and how is a different discussion.)
- Some parameters will probably need a new property to be saved in, which the `AutomationEditor` will translate to a note/synth parameter depending on its editing layout
- The shortcut for calling the Launch Pad doesn't feel very consistent currently as it's either on the song or clip button. Ideally this would live on the same button wherever it can be called

#### Views
- The current general strategy is to create global variables for all the main views and reuse them when activated. Is it safer/easier to refactor them to static properties?
- None of the views inherit from view.h, which could be assumed would be the case from how the files are named. Instead all views inherit from `TimelineView`
- `ClipNavigationTimelineView` is a `TimelineView` and only implements a few methods. `TimelineView` itself is only used as a base class for `ArrangerView`. This could probably be simplified into one class
- **Proposal:**
  - Combine `TimelineView` and `ClipNavigationTimelineView` to a new `View` class. This is a `RootUI` and will remain the base class of all other views. Shared behavior for views lives here
  - The current `View` holds three types of behavior, which should all be relocated to their appropriate locations:
    - View-specific behavior moves to their respective views, where possible as overrides from the new `View`
    - Shared behavior between all views moves to the new `View` class
    - Global UI behavior really doesn't belong in the views folder at all, should probably move to `src/ui/ui.cpp`
  - *I have yet to dive into the code rigorously enough to realise whether or not there are any code-breaking issues that prevent this solution*
- *Song View* is called `SessionView` in the repo
- *Song Row View* and *Song Grid View* both live in `SessionView` and are probably different enough to live in their own separate files.
- **Proposal:**
  - Rename `SessionView` to `SongView`
  - Separate unique behavior for *Song View, Row Layout* and *Song View, Grid Layout* into `song_view_grid_layout.h` and `song_view_row_layout.h`
  - Rename `PerformanceSessionView` to `PerformanceView`
  - Refactor methods and properties accordingly
- `PerformanceSessionView` is not a `SessionView` 
- **Proposal:**
  - Rename `PerformanceSessionView` to `PerformanceView`
- `AutomationView` is too large to manage
- **Proposal:**
  - Refactor `AutomationView` to a base-class which can be inherited from in different contexts, behavior can be organized in child-classes

#### UI
- There's a whole bunch of global stuff here. It would probably help legibility in other parts of the code if these were captured in a (static) class. UI mode defines could be in an enum : uint32_t for the same reason.
