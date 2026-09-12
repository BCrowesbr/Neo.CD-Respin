# Neo.CD Respin


https://github.com/niuus/NeoCDRX/

Neo.CD Respin is a direct fork of NeoCD-RX (NiuuS) and a continuation of the work of several developers who dedicated their time and effort to Neo Geo CD emulation on the Wii: 
NeoCD-RX (NiuuS), NEO-CD REDUX (Softdev), NeoGeo CD Redux (Infact), NeoCD-Wii (Wiimpathy / Jacobeian), and NeoCD Redux Unofficial (megalomaniac). NEO-CD REDUX was itself based on 
NeoCD/SDL 0.3.1 (Foster) and the NeoGeo CDZ (NJ) emulator, which are source ports of the original NeoCD emulator (Fabrice Martinez).
As with any other Wii emulator distributed as homebrew, you need a console capable of running homebrew applications. All tests of this fork were performed using The Homebrew Channel 
1.1.2. Based/forked from: https://github.com/niuus/NeoCDRX

Neo.CD Respin is distributed under the GNU Lesser General Public License v2.1 (LGPL-2.1). 


::::::: WHY ANOTHER FORK? ::::::::::::::::::::::::::::::::::::::::::

This version grew out of my interest in emulation on the Wii. Even with so many options available today, the Wii remains one of the most affordable ways to play older games on a CRT 
with very good image quality. NeoCD-RX is the best emulator for this purpose, but I missed a few features that made my experience on CRTs somewhat frustrating. I had always thought it
 would be interesting if the emulator could run at 240p and, at the same time, provide a more convenient experience while taking the Wii's particular characteristics into account. 
Out of curiosity, I used an AI-assisted environment to compile the project and experiment with changes to the original source code, and I found that these changes were viable when 
implemented carefully. This allowed me to implement a number of improvements that greatly improved my experience with the emulator. I believe making the result public is worthwhile so 
the community can see it, but above all I hope it encourages other people to revisit what has already been created for the Wii and explore improvements that can further enhance the 
experience. Once again, all credit goes to NiuuS and the other developers for the work done over the years. Without their work, this fork would never have been possible. Thank you very 
much.

IMPORTANT: Everything in this fork was designed with Wii users playing on CRTs through composite video or S-Video in mind. I did not focus on the experience on modern TVs, and I have 
not tested the GameCube version or vWii. I currently do not have the hardware setup required for those tests, although this may change in the future.


::::::: FEATURES ::::::::::::::::::::::::::::::::::::::::::

All features inherited from the original emulator are the work of NiuuS and the previous developers. The improvements I implemented in Neo.CD Respin are:
● Native 240p output and selectable video modes;
● CUE/BIN and CDDA support;
● CD player integrated into the emulator;
● Customizable control mapping;
● Wii Classic Controller support;
● Always Open Source!


::::::: CHANGELOG ::::::::::::::::::::::::::::::::::::::::::

[1.0 - September 12, 2026]
● Native 240p output, with the option to switch to 480i through the menu;
● Fixed side-screen rendering in some games that previously showed cropped graphics or black vertical bars, allowing the full intended game image to be displayed;
● CUE/BIN disc image support with integrated CDDA audio;
● Optimized CDDA buffering and audio synchronization;
● Expanded SFX and music equalization options; the equalizer now processes the channels separately;
● Wii Classic Controller support;
● Controller remapping; the original NeoCD-RX default mappings are preserved for the controllers it already supported, while Neo.CD Respin adds a default profile for the Wii C
lassic Controller. Compatible controllers can now be remapped as desired. This is especially useful for third-party controllers connected through BlueRetro. The remapping system 
also recognizes the GameCube Controller C-Stick directions as assignable inputs, expanding the available customization options;
● All newly implemented settings, as well as the existing settings, are now saved to a .cfg file in the NeoCDRE data folder alongside the BIOS and game directories;
● Improved game-list navigation: Up and Down move through the list one game at a time, while Left and Right move backward or forward in blocks of eight games;
● Added an integrated CD Player accessible from the menu. After a game has been loaded, returning to the options menu makes the CD Player available for listening to the disc's audio 
tracks without closing the running game.

[For additional information about NeoCD-RX and its original features, please refer to the original NeoCD-RX documentation by NiuuS] 
https://github.com/niuus/NeoCDRX/blob/main/NeoCDRX_manual.pdf


::::::: INSTALLATION AND USE ::::::::::::::::::::::::::::::::::::::::::

1. Extract the contents of the NeoCDRE package to the root of the SD card or USB device.
2. Neo.CD Respin does not include a Wii Channel or forwarder. Launch the emulator through the Homebrew Channel, or use a compatible forwarder installed separately.
3. CUE/BIN games must be placed in the "\NeoCDRE\games" folder.
4. A compatible BIOS is required. Place it in "\NeoCDRE\bios" and rename it to "NeoCD.bin".

The emulator accepts the following BIOS images:

Neo Geo CDZ BIOS (NeoCD.bin)
Size: 524.288 bytes
CRC32: DF9DE490
MD5: F39572AF7584CB5B3F70AE8CC848ABA2
SHA-1: 7BB26D1E5D1E930515219CB18BCDE5B7B23E2EDA

Neo Geo CDZ BIOS (NeoCD.bin)
Size: 524.288 bytes
CRC32: 33697892
MD5: 11526D58D4C524DAEF7D5D677DC6B004
SHA-1: B0F1C4FA8D4492A04431805F6537138B842B549F

Once you are done, you can proceed to run the emulator. 


::::::: CONFIGURATION ::::::::::::::::::::::::::::::::::::::::::

To configure Neo.CD Respin, press 'A' on the "Settings" box. The following options are available:
• "Region" will allow you to change the emulated console region, to access other
languages and in some cases, change or uncensor game content (fatalities, blood,
difficulty, lives, title screens, etc.). Reload the game (not reset) for the
setting to take effect.

• "Save Device" offers two options, use "SD/USB" to save the SRAM memory
(sort of a virtual memory card implemented inside the real Neo Geo CD console)
directly to the media drive, or use "MEM Card" to save to a physical GameCube
Memory Card, as you would on a real Neo Geo AES, to take your progress to
another console, or just for the nostalgia factor.

• "TV Mode" switches the video mode between 240p and 480i.

• "SFX / Music" allows you to raise the volume on sound FX or CDDA
tracks, or adjust the Low / Mid / High frequency bands separately.

• "Controller Mapping" allows you to remap commands for any supported controller or restore the default mapping.


::::::: CUE/BIN SUPPORT ::::::::::::::::::::::::::::::::::::::::::

Neo.CD Respin supports CUE/BIN disc images. Games must be uncompressed and placed in the "\NeoCDRE\games"  folder on the SD card or USB device.
The .cue file and all corresponding .bin files must be kept together in the same game folder.
Unlike the original NeoCD-RX setup, which used extracted game files and converted MP3 tracks, Neo.CD Respin plays the CD audio tracks (CDDA) directly from the CUE/BIN image, 
preserving the original disc audio structure.


::::::: SUPPORTED CONTROLLERS ::::::::::::::::::::::::::::::::::::::::::

Neo.CD Respin currently supports the following:
 • Wii Remote (horizontal)
 • Wii Remote Plus (or Wii MotionPlus adapter)
 • Wii Remote+Nunchuk
 • Wii Classic Controller
 • GameCube controller


::::::: DEFAULT MAPPINGS ::::::::::::::::::::::::::::::::::::::::::

GameCube Controller
  Neo Geo A = B
  Neo Geo B = A
  Neo Geo C = Y
  Neo Geo D = X
  Neo Geo Select = Z
  Neo Geo Start = START
  Neo Geo directions = Dpad or Analog Stick
  Force Memory Card Saving = R
  Emu Menu = L

Wii Remote (horizontal)
  Neo Geo A = 1
  Neo Geo B = 2
  Neo Geo C = B
  Neo Geo D = A
  Neo Geo Select = MINUS (-)
  Neo Geo Start = PLUS (+)
  Neo Geo directions = Dpad (horizontal)
  Force Memory Card Saving = MINUS (-) and PLUS (+) together
  Emu Menu = Home

Wii Remote + Nunchuk
  Neo Geo A = A
  Neo Geo B = B
  Neo Geo C = PLUS (+)
  Neo Geo D = 1
  Neo Geo Select = MINUS (-)
  Neo Geo Start = PLUS (+)
  Neo Geo directions = Analog Stick
  Force Memory Card Saving = MINUS (-) and PLUS (+) together
  Emu Menu = Home

Wii Classic Controller
  Neo Geo A = B
  Neo Geo B = A
  Neo Geo C = Y
  Neo Geo D = X
  Neo Geo Select = MINUS (-)
  Neo Geo Start = PLUS (+)
  Neo Geo directions = DPad or Analog Stick
  Force Memory Card Saving = R
  Emu Menu = Home


::::::: CREDITS & THANKS ::::::::::::::::::::::::::::::::::::::::::

• NeoCD-RX (NiuuS)
• NeoCD-Wii (Wiimpathy / Jacobeian)
• NeoCD Redux Unofficial (megalomaniac)
• NeoGeo CD Redux (Infact)
• NEO-CD REDUX (softdev)
• NeoCD/SDL 0.3.1 (Foster)
• NeoGeo CDZ (NJ)
• NeoCD 0.8 (Fabrice Martinez)
• [M68000 C Core](https://github.com/kstenerud/Musashi) (Karl Stenerud)
• [MAME Z80 C Core](https://github.com/mamedev/mame/tree/master/src/devices/cpu/z80) (Juergen Buchmueller)
• Sound Core (MAMEDev.org)
• The EQ Cookbook (Neil C / Etanza Systems)
• The EQ Cookbook (float only version code - Shagkur)
• WKF & IDE-EXI V1 (code from [Swiss GC](https://github.com/emukidid/swiss-gc) - emu_kidid)
• libMAD (Underbit Technologies)
• libZ (zlib.org)
• TehSkeen forum (2006-2009)
• NeoCDRX emu bg - Style 1 (catar1n0)
• NeoCDRX menu design (NiuuS)


::::::: RELEVANT LINKS ::::::::::::::::::::::::::::::::::::::::::

● Original emulator: https://github.com/niuus/NeoCDRX/releaseshttps://github.com/niuus/NeoCDRX/releases

