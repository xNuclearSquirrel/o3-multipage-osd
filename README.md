# o3-multipage-osd
Mod for enabling multipage OSD (up to 3 pages) for the O3 on the V2 goggles.
This mod combines and replaces two previous mods:
- o3-osd-record
- o3-custom-fonts

important to uninstall these first!

## NOTES
- The mod opens BMP images from the SD Card and uses them as the OSD font resource.
- The mod also directly interprets the msp DisplayPort stream and enables the use of multiple font pages.
- version 1.1.0 supports up to 4 fontpages. ~~Use fonts in /fonts_1.1.0/.~~- all font in /fonts/
- Using smaller fonts (e.g. a 1 page font when the FC expects 4 pages), will just remap the symbols of higher pages to page 1. So you can use a 1 page Betaflight font if you don't want to use different colors.
- You can use your own fonts, just make sure it is a v3 BMP object with 32bit depth (alpha channel) and no compression, or use SNEAKY_FPV's fonts in the fonts folder.
- This mod also records the OSD to a .osd file. This is always enabled. A tool for the overlay is coming soon.
- Font selection is done by placing the fonts you want to use int /fonts/ on the SD card.
- You put several fonts into the /fonts/ folder and then switch between them by long pressing the back button on the goggles (hold for ~7s).

##### Install
- coming soon 

##### Manual install
- `adb push o3-multipage-osd_1.1.0_pigeon-glasses-v2.ipk /tmp`
- open up shell with `adb shell`
- `opkg install /tmp/o3-multipage-osd_1.1.1_pigeon-glasses-v2.ipk`
- or `opkg upgrade /tmp/o3-multipage-osd_1.1.1_pigeon-glasses-v2.ipk` to upgrade an older version.

##### Configuration
- copy all the fonts you want to use (max. 10) into the /fonts/ directory on your SD card. **This applies to ver. 1.1.1 onwards: font.txt is no longer needed!** If you are on an older version please update.
- If you are using INAV then in the OSD tab select "AVATAR" to get the 53*20 grid. For Betaflight simply pick HD.
- Insert the SD card into the goggles and it should automatically enable the custom font.
- To toggle between fonts in the /fonts/ folder press the back button (next to the record button) and hold for roughly 7 seconds.

##### Known issues
- ~~You need to manually select the font! Automatic font selection based on the FC firmware is not supported! If you switch between INav and Betaflight frequently consider having two SD cards with differnt fonts to swap in and out.~~ fixed in 1.1.0 by having a switch button.
- ~~Only max. 3 font pages are supported.~~ fixed in 1.1.0 through horizontal page alignment.
- If the font is not as it should be, or the SD card is missing the mod will always try to default to the standard DJI OSD rendering.

- **Test carefully this is by no means well and thoroughly tested. There could be overallocations of memory and you could run into issues such as SD speed low warnings. If you encounter anything strange please report it!**


##### Credits
- Thanks to Joonas for the help when developing this and SNEAKY_FPV for letting me use his fonts!
