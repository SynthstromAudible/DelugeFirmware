# Midi Follow Mode with Midi Feedback

## Description:

Master midi follow mode whereby after enabling the mode, you set a master midi follow channel for synth clips, kit clips, and for parameters and all midi (notes + cc’s) received on the channel relevant for the active context will be directed to control the active view (e.g. arranger view, song view, audio clip view, instrument clip view). 

- For example, Midi Notes received on the Synth Channel will send notes to the active Synth clip. Midi CC's received on the Param Channel will send cc's to control the params of the active view.

Comes with a midi feedback mode to send updated parameter values on the midi follow channel for learned midi cc's. Feedback is sent whenever you change context on the deluge and whenever parameter values for the active context are changed.

Comes with an XML file with default CC to Deluge parameter mappings. You can customize this XML file to map CC's differently as required.

**Simple summary:** Set your channel(s), set your Midi Controller(s) to the same channel(s), set a root note for your kits, confirm that your controler cc's are mapped to the parameters you want (via MIDIFollow.XML) play and control the deluge instruments and parameters with ease!

**No more re-learning your Midi controllers every time you start a new song, add new clips or change instrument presets.**

- Note: Your existing midi learnings will remain untouched and can be used together with the master midi follow mode.

### **Turn Follow Mode On, Set Master Midi Follow Channel(s), Set Kit Root Note, Enable/Disable Pop-ups and Enable/Disable Midi Feedback **

To turn configure the various Midi Follow Mode settings, enter the settings menu and go to the sub menu for Midi -> Midi-Follow

- In the Midi-Follow > Follow submenu, set this feature to Enabled or Disabled
- In the Midi-Follow > Channel submenu, set the channel between 1 and 16 for Synths, Kits and Params.
- In the Midi-Follow > Kit Root Note submenu, set the root note for kits between 1 and 127 in order to map Midi Notes received to Kit rows. The root note corresponds to the bottom row in a Kit.
- In the Midi-Follow > Display Param submenu, enable or disable param pop-ups
- In the Midi-Follow > Feedback > Feedback submenu, enable or disable midi follow feedback
- In the Midi-Follow > Feedback > Automation Feedback submenu, enabled or disable midi follow feedback for automated parameters and set the rate at which feedback for automated parameters is sent
- In the Midi-Follow > Feedback > Filter Responses submenu, enable or disable filtering of responses received within 1 second of sending a midi feedback value update. 

<img width="240" alt="Screen Shot 2023-12-02 at 7 13 16 PM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/f019b488-11e8-442d-a47b-cd95fbc45d9b">

### **Notes:**
For notes, the functionality is simple: notes received on the master midi channel will play the instrument in the active clip (e.g. a synth, or all kit rows).

- Note 1: You can play a synth or kit clip without entering the clip from arranger or song view. Simply press and hold the clip in arranger or song view to preview the clip (as you would to change the parameters of that clip with the gold encoders) and then send notes from your midi controller.

- Note 2: For Kit's, the bottom Kit row is mapped by default to the root note C1 (note # 36). All kit rows above are mapped to note's incrementally (e.g. 36, 37, 38, etc.). This kit root note # is configurable (from 0 to 127) through the Kit Root Note submenu.

### **CC's:**
Midi CC mappings for Midi Follow Mode are saved to the root of your SD card in an XML file called MIDIFollow.XML

Within MIDIFollow.XML, all Parameters that can mapped to a Midi CC are listed. The Midi CC value is enclosed between a Parameter XML tag - e.g. `<lpfFrequency>49</lpfFrequency>` indicates that Midi CC is mapped to the LPF Frequency parameter. Conversely when a value of 255 is entered (e.g. `<hpfFrequency>255</hpfFrequency>`) it indicates that no Midi CC value has been mapped to that parameter.

<img width="478" alt="Screen Shot 2023-12-02 at 4 34 59 PM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/674ace78-3b31-4a34-b3d0-15832b204b74">

You can manually edit the MIDIFollow.XML to enter your Midi CC mappings to each Parameter. 

The defaults from MIDIFollow.XML are loaded automatically when you start the Deluge so you can begin controlling the deluge with your MIDI controller right away without any need to enter a menu or Midi Learning View. 

Note: A parameter can only be learned to one midi CC. Conversely, a midi CC can be learned to multiple parameters.

The parameters are controlled only in the current context.

- So if you are controlling filter, for example, while in song view it will only control the song’s filter. If you enter a specific synth clip, it will only control that synths filter, if you are in a kit clip it will either control the entire kit or a specific row in that clip (depending on whether you have affect entire enabled or not)
- In other words it checks what context you’re in and controls the parameters of that context.

Note: You can control the parameters of a synth or kit clip without entering the clip from arranger or song view. Simply press and hold the clip in arranger or song view to preview the clip (as you would to change the parameters of that clip with the gold encoders) and then send midi cc's from your midi controller.

#### Display Parameter Names and Values on Screen

> Note: Midi pop-up's can be disabled in the Midi Follow Display Param submenu.

For learned Midi CC's, I have added a pop-up on the display whenever Midi CC's are received to indicate the name of the parameter that is being controlled by that Midi CC and the current value being set for that parameter.

<img width="170" alt="Screen Shot 2023-12-04 at 2 32 25 AM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/f4e8115c-c2af-4cfe-94cf-d2e117201cd5">

Note: if the Midi CC being received for a Parameter that cannot be controlled in the current context (e.g. trying to control Attack while in Song View), the pop-up message will say "Can't Control: <Parameter Name".

<img width="191" alt="Screen Shot 2023-12-04 at 2 32 03 AM" src="https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/b2dcefb2-4f90-4b23-804c-3250bfd24862">

## Re-cap of functionality

1. Added new Midi submenu for Midi-Follow where you can: 

- Enable or Disable Midi Follow Mode
- Set the Midi Follow Channel for Synth Notes
- Set the Midi Follow Channel for Kit Notes
- Set the Midi Follow Channel for Param CC's
- Set the Midi Follow Root Note for Kits
- Enable or Disable Midi Follow Param Pop-up's
- Enable or Disable Midi Follow Feedback
- Enable or Disable Midi Follow Feedback for Automated Parameters and set the Midi Feedback Update Rate
- Enable or Disable Midi Follow Feedback Filtering of Midi CC responses received within 1 second of sending feedback
2. When Midi Follow Mode is enabled, a Midi Follow Channel has been set, and you have mapped your Midi CC's, your external controller's Notes and Midi CC's will be automatically directed to control the Notes of the Active Instrument (e.g. Synth, Kit, Midi, CV) or the Parameters of the Active View (e.g. Song View, Arranger View, Audio Clip View, Instrument Clip View).
3. By default, the root note for kit's is C1 for the bottom kit row but this can be configured in the Midi Follow menu.
4. Pop-up's are shown on the display for mapped Midi CC's to show the name of the parameter being controlled and value being set for the parameter. This can be disabled in the menu.
5. Midi feedback is sent for mapped CC's when the active context changes or you change the value of a mapped parameter on the deluge (e.g. using mod encoders or select encoder if you're int he menu). Midi feedback can be disabled in the menu.
6. Midi feedback for automated parameters can also be sent and can be enabled or disabled in the midi feedback sub menu. When enabled, you choose between 3 speeds at which to send feedback for automated parameters: Low (500 ms), Medium (150 ms), High (40 ms). Sending automated parameter feedback can be taxing on the deluge midi output system, so depending on the amount of automation you do, you may want to adjust the speed (e.g. slow it down) to not affect the performance of the Deluge.
7. Midi feedback can cause an undesirable result with certain applications when the application responds back to the Deluge after the Deluge has sent it an updated value (Loopy Pro and Drambo on iPad are known to do this). This can cause lag in the deluge and potential feedback loops. To handle this, a toggable filter was added which ignores messages received for the same ccNumber within 1 second of sending a midi feedback update. If the application receiving the midi feedback update does not send responses back to the Deluge, then this setting should be set to Disabled in the Midi Feedback Filter Responses sub menu.
