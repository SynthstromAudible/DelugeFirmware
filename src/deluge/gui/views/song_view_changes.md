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
2. What is the scope of macros?
3. Move grid layout enums to grid class (private), after splitting layouts into separate files
4. Renamed SongViewLayout to SongViewLayoutSelection to make room for enum class SongViewLayout. Not sure what SongViewLayoutSelection actually represents?
5. Check the merit of `offset` in `selectLayout`. It comes from the song button + turn select. Should probably be a toggle method to toggle between launch and edit modes
6. There are three 7SEG methods in `song_view` that should be abstracted to `view.h`
7. Checking whether buttons are held or not should really happen in UI and not in any view
8. We need low-level and consistent handling of sidebar pads
