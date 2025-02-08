01/2025 Mapping for new params including Arpegiator

- Use default mapping on deluge.
- Use MidiFighter Twister Utility to open the mapping file, transfer it to the device or change controls midi channels.
- If there are any problems try MF Twister Firmware 02.Oct.2019 (current firmware was not working for me but forgot why :) 
- Used System channel for switching bank pages: 16 (default was: 4)
  -> Change banks via push encoder buttons in top row
- Encoder channel for cc-params: 10
- Button commands in the two bottom rows channel:11 - are mapped for same behaviour on all 4 banks - learn them in deluge settings menu for midi commands


Bank 1 - Main, Filt, Eq, Reverb, Delay ----------------------
Vol         LPF          HPF          DelTime    
CompThresh  LPRes        HPRes        DelAmount
MastPitch   LPMorph      HPMorph      Reverb
EQBass      EQTreb       Dist-Bit     Dist-Decim

Bank 2 - Efx, Arp --------------------------------------------------
ModFx-Rate    Dist-WavFld  Arp-RatchetAmount  Arp-RatchetProb       
ModFx-Depth   Arp-Oct      Arp-ChordPoly      Arp-ChordProb       
ModFx-Fdbck   Arp-Gate     Arp-NoteProb       Arp-BassProb	 
ModFx-Offset  Arp-Rate	   Arp-Rytm           Arp-SeqLen         		

Bank 3 - Envelopes, LFO, Sidechain, Noise  ----------------------------
Env1-Attck	   Env2-Attck	   LFO1-Rate     LFO2-Rate
Env1-Decay     Env2-Decay                    StuttRate
Env1-Sust	   Env2-Sust       Side-Shape    Portamento           
Env1-Release   Env2-Release	   Side-Lvl      Noise

Bank 4 - Synth: Osc, FM -------------------------------------
Osc1-fdbck	Osc1-WavePos	Osc2-fdbck    Osc2-WavePos
Osc1-PW		FM1-FdBck	    Osc2-PW	      FM1-FdBck	
Osc1-Pitch	FM1-Pitch	    Osc2-Pitch    FM1-Pitch
Osc1-Lvl	FM1-Lvl		    Osc2-Lvl      FM1-Lvl	


Push-Encoder switches ---------------------------------------   
Top row switches Banks:
Bank 1          Bank 2          Bank3         Bank 4  

Two bottom rows for deluge commands on all pages (Ch11 1..8) --------------------
-> Learn in Deluge via global command Settings - Example:
Restart	 Fill	LayerLoop	Redo
Play     Rec    Loop		Undo


