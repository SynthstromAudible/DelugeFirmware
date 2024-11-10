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

# To remember:
1. Remove macros from gridlayoutmodes
2. Move grid layout enums to grid class (private), after splitting layouts into separate files
3. Renamed `SongViewLayout` to `SongViewLayoutSelection` to make room for `enum class SongViewLayout`. Not sure what `SongViewLayoutSelection` actually represents? The value stored here in defaults also seems to be the wrong type
4. Check the merit of `offset` in `selectLayout`. It comes from the song button + turn select. Should probably be a toggle method to toggle between launch and edit modes
5. There are three 7SEG methods in `song_view` that should be abstracted to `view.h`
6. Checking whether buttons are held or not should really happen in `UI` and not in any view
7. We need low-level and consistent handling of sidebar pads

# To remember, outside the scope of this branch:
1. Consistent naming for square/pad
2. Custom class for pad coords
