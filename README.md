# o3-multipage-osd
Mod for enabling multipage OSD (up to 3 pages) for the O3 on the V2 goggles.
This mod combines and replaces two previous mods:
- o3-osd-record
- o3-custom-fonts

important to uninstall these first!

## NOTES
- The mod opens BMP images from the SD Card and uses them as the OSD font resource.
- The mod also directly interprets the msp DisplayPort stream and enables the use of multiple font pages.
- There is an image size limit in the goggles, which currently restricts the fonts to 3 pages max. Using larger fonts will cause them not to be displayed art all.
- Using smaller fonts (e.g. a 1 page font when the FC expects 4 pages), will just remap the symbols of higher pages to page 1. So you can use 1 page betaflight fonts if you don't want the color options.
- You can use your own fonts, just make sure it is a v3 BMP object with 32bit depth (alpha channel) and no compression, or use SNEAKY_FPV's fonts in the fonts folder.
- This mod also records the OSD to a `.osd` file. This is always enabled.
- Font selection is done by writing the font name into font.txt. If you create `font.txt` and leave it empty you disable the mod.

##### Install
- coming soon 

##### Manual install
- `adb push o3-multipage-osd_1.0.0_pigeon-glasses-v2.ipk /tmp`
- open up shell with `adb shell`
- `opkg install /tmp/o3-multipage-osd_1.0.0_pigeon-glasses-v2.ipk`
- or `opkg upgrade /tmp/o3-multipage-osd_1.0.0_pigeon-glasses-v2.ipk` to upgrade an older version.

##### Configuration
- copy fonts to the SD card root directory
- select font by writing the font name into font.txt (e. g. INAV_nexus, BTFL_sphere)
- If you are using INAV, select "AVATAR" in the OSD tab to get the 53*20 grid.

##### Known issues
- You need to manually select the font! Automatic font selection based on the FC firmware is not supported! If you switch between INAV and Betaflight frequently consider having two SD cards with differnt fonts to swap in and out.
- Only max. 3 font pages are supported.
- If the font is not as it should be, or the SD card is missing the mod will always try to default to the standard DJI OSD rendering. But test carefully.

##### Credits
- Thanks to Joonas for the help when developing this and SNEAKY_FPV for letting me use his fonts!
