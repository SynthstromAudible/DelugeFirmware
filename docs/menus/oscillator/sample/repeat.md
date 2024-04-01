Sets the repeat mode for the currently selected sample, if there is one.

Options are:
 - <string-for name="STRING_FOR_CUT>CUT</string-for>: The sample is cut when the note release.
 - <string-for name="STRING_FOR_ONCE>ONCE</string-for>: The sample plays to its END point regardless of note event
   length.
 - <string-for name="STRING_FOR_LOOP>LOOP</string-for>: The sample loops within its specified loop region once playback
   reaches the loop end, if a loop region is configured.
 - <string-for name="STRING_FOR_STRETCH>STRETCH</string-for>: The sample is stretched to match the note event length.

Changing this setting will automatically change the PITCH/SPEED setting as well -- enabling pitch/speed linking unless
<string-for name="STRING_FOR_STRETCH>STRETCH</string-for> is selected in which case pitch/speed are unlinked.
