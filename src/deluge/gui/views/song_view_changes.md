# Changes
1. Various renames:
   1. File names to `song_view.h` & `song_view.cpp`
   2. Global `sessionX` to `songViewX`
   4. `UIType::SESSION` to `UIType::SONG_VIEW`
   5. Used enums are now:
      - `SongViewLayout` (rows/grid)
      - `SongViewGridLayoutMode` (launch/edit/macros)
      - `SongViewGridLayoutModeSelection` (select/defaultLaunch/defaultEdit)
      - `SongViewGridLayoutModifierPad` (defines right sidebar pad functions, renamed from colors to descriptive names)
2. Make all relevant enums scoped for safety and legibility, shorten element names
3. Roughly organize properties/methods, label in obnoxiously large comments (ongoing project)
4. Remove configuring macros as a `SongViewGridLayoutMode`
   - Configuring macros can be done while in edit mode, launch mode, or even in rows layout where those modes don't exist. This makes configuring macros not analogous to edit mode and launch mode. I've changed this as a preparation for further refactoring of GridLayout and its modes
5. Refactor macros enums
   - With "kind" and "type" being mostly synonymous, I feel like type is much more consistently used in coding and other technical environments, so I renamed to type
   - Used TitleCase


# To remember:
1. Where do macro definitions belong? Privately in song?
2. Look at the `previousLayout` implementation
3. Change `gridHandlePads` switch statement
4. Move grid layout enums to grid class (private), after splitting layouts into separate files
5. Renamed `SongViewLayout` to `SongViewLayoutSelection` to make room for `enum class SongViewLayout`. Not sure what `SongViewLayoutSelection` actually represents? The value stored here in defaults also seems to be the wrong type
6. Check the merit of `offset` in `selectLayout`. It comes from the song button + turn select. Should probably be a toggle method to toggle between launch and edit modes
7. There are three 7SEG methods in `song_view` that should be abstracted to `view.h`
8. Checking whether buttons are held or not should really happen in `UI` and not in any view
9.  We need low-level and consistent handling of sidebar pads

# To remember, outside the scope of this branch:
1. Consistent naming for square/pad
2. Custom class for pad coords
