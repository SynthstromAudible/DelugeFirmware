#### Vuefinder

Vuefinder is a web-based "cloud" file browser written by Yusuf Özdemir (https://github.com/n1crack/vuefinder). Jamie Fenton has adapted this program to allow manipulation of the Deluge SD card contents from a web browser using Midi System Exclusive over USB.

To use this, connect a computer to the Deluge using an USB cable. Then visit a web page which has the Vuefinder (adapted for Deluge SysEx) on it. One URL that works at present is https://www.fentonia.com/deluge/vuefinder/. (This will likely change). At the top of the page you should see Midi: in: [Deluge Port 1] out: [Deluge Port 1]. If those values are "None", check the cable and the Deluge and reload the web page.

When the connection is established, press the "home" icon and you should see a listing of the contents of the SD card in the Deluge.

In the upper right hand corner is an icon made out of 4 rectangles. Press that to switch from icons to list file format. Clicking on a directory entries descends. Clicking on the up arrow ascends. The other icon in the upper-left corner which has four arrows all pointing out (or in) lets you enlarge or contract the page (which can hide the Midi in and out popups).

The folder icon with a '+' in it will create a new directory. If you select an entry it will be highlighted and you can then rename it using the "folder with a pencil". The trash can icon deletes a file or directory. (You will have to confirm such actions).

If you hold the control key down (on the Mac, not sure about Windows/Linux), you will see a brief drop down menu with entries like "Preview", "Download", "Rename", & "Delete". Selecting Download will download a file or a directory of files. You will be asked where to put that.

Selecting Preview with a text or XML file will load the file into a preview dialog. You can also open the Preview dialog by just double-clicking on a file entry in the directory listing.

Some Preview file types will also show an "Edit" link in the upper left hand corner. Pressing that opens another dialog which lets you change the text contents which you can then "Save" or "Cancel". The preview dialog also lets you "Download", which again has you choose a destination directory.

Previewing a .WAV audio file causes the audio file to be loaded into the "Peaks" media player for audition. Note that .WAV files can be large and the SysEx connection is limited to around 200K bytes per second, or about 5+ seconds per megabyte.

To upload, press the icon that looks like a tray with an up-arrow in it. You can choose a file, a directory either by browsing or "drag and drop". You should be able to upload a directory with its nested internals if desired.

All of the credit for Vuefinder should go to Yusuf. He has made an outstanding file browser. Clicking on the circle with question mark at the lower right hand side will bring up an info page which includes "About" information, a Settings dialog, a Shortcuts list, and a Reset feature. The Settings dialog allows you to choose a dark or light Theme and Language, among 12 options which include Arabic, English, French, German, Persian, Hindi,  Russian, Swedish, Turkish, and Chinese. Unfortunately New Zealand is not an option yet.

Among the many loose-ends not yet addressed include an Abort feature to stop a long transfer and a progress meter that shows how long it will take. If you need to stop a transfer in its tracks, reloading the web page will work. (If you do too many of these forced-resets, you may have to reboot the Deluge too).

There is an icon on the second row, at the far left that lets you deal with "pinned" folders. This does not actually work yet.
