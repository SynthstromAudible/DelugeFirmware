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

### Code Cleanup
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
- `PerformanceSessionView` is not a `SessionView` 
- **Proposal:**
  - Rename `SessionView` to `SongView`
  - Separate unique behavior for *Song Row View* and *Song Grid View* into `song_view_grid.h` and `song_view_row.h`
  - Rename `PerformanceSessionView` to `PerformanceView`
  - Refactor methods and properties accordingly
